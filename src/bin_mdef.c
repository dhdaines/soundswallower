/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 2005 Carnegie Mellon University.  All rights 
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*********************************************************************
 *
 * File: bin_mdef.c
 * 
 * Description: 
 *	Binary format model definition files, with support for
 *	heterogeneous topologies and variable-size N-phones
 *
 * Author: 
 * 	David Huggins-Daines <dhuggins@cs.cmu.edu>
 *********************************************************************/

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <soundswallower/prim_type.h>
#include <soundswallower/ckd_alloc.h>
#include <soundswallower/byteorder.h>
#include <soundswallower/case.h>
#include <soundswallower/err.h>
#include <soundswallower/mdef.h>
#include <soundswallower/bin_mdef.h>
#include <soundswallower/export.h>

static cd_tree_t *
build_cd_tree_from_mdef(bin_mdef_t *bmdef, mdef_t *mdef)
{
    int i, nodes, ci_idx, lc_idx, rc_idx;

    /* Walk the wpos_ci_lclist once to find the total number of
     * nodes and the starting locations for each level. */
    nodes = lc_idx = ci_idx = rc_idx = 0;
    for (i = 0; i < N_WORD_POSN; ++i) {
        int j;
        for (j = 0; j < mdef->n_ciphone; ++j) {
            ph_lc_t *lc;

            for (lc = mdef->wpos_ci_lclist[i][j]; lc; lc = lc->next) {
                ph_rc_t *rc;
                for (rc = lc->rclist; rc; rc = rc->next) {
                    ++nodes;    /* RC node */
                }
                ++nodes;        /* LC node */
                ++rc_idx;       /* Start of RC nodes (after LC nodes) */
            }
            ++nodes;            /* CI node */
            ++lc_idx;           /* Start of LC nodes (after CI nodes) */
            ++rc_idx;           /* Start of RC nodes (after CI and LC nodes) */
        }
        ++nodes;                /* wpos node */
        ++ci_idx;               /* Start of CI nodes (after wpos nodes) */
        ++lc_idx;               /* Start of LC nodes (after CI nodes) */
        ++rc_idx;               /* STart of RC nodes (after wpos, CI, and LC nodes) */
    }
    E_INFO("Allocating %d * %d bytes (%d KiB) for CD tree\n",
           nodes, sizeof(*bmdef->cd_tree), 
           nodes * sizeof(*bmdef->cd_tree) / 1024);
    bmdef->n_cd_tree = nodes;
    bmdef->cd_tree = ckd_calloc(nodes, sizeof(*bmdef->cd_tree));
    for (i = 0; i < N_WORD_POSN; ++i) {
        int j;

        bmdef->cd_tree[i].ctx = i;
        bmdef->cd_tree[i].n_down = mdef->n_ciphone;
        bmdef->cd_tree[i].c.down = ci_idx;
        E_DEBUG("%d => %c (%d@%d)\n",
                i, (WPOS_NAME)[i],
                bmdef->cd_tree[i].n_down, bmdef->cd_tree[i].c.down);

        /* Now we can build the rest of the tree. */
        for (j = 0; j < mdef->n_ciphone; ++j) {
            ph_lc_t *lc;

            bmdef->cd_tree[ci_idx].ctx = j;
            bmdef->cd_tree[ci_idx].c.down = lc_idx;
            for (lc = mdef->wpos_ci_lclist[i][j]; lc; lc = lc->next) {
                ph_rc_t *rc;

                bmdef->cd_tree[lc_idx].ctx = lc->lc;
                bmdef->cd_tree[lc_idx].c.down = rc_idx;
                for (rc = lc->rclist; rc; rc = rc->next) {
                    bmdef->cd_tree[rc_idx].ctx = rc->rc;
                    bmdef->cd_tree[rc_idx].n_down = 0;
                    bmdef->cd_tree[rc_idx].c.pid = rc->pid;
                    E_DEBUG("%d => %s %s %s %c (%d@%d)\n",
                            rc_idx,
                            bmdef->ciname[j],
                            bmdef->ciname[lc->lc],
                            bmdef->ciname[rc->rc],
                            (WPOS_NAME)[i],
                            bmdef->cd_tree[rc_idx].n_down,
                            bmdef->cd_tree[rc_idx].c.down);

                    ++bmdef->cd_tree[lc_idx].n_down;
                    ++rc_idx;
                }
                /* If there are no triphones here,
                 * this is considered a leafnode, so
                 * set the pid to -1. */
                if (bmdef->cd_tree[lc_idx].n_down == 0)
                    bmdef->cd_tree[lc_idx].c.pid = -1;
                E_DEBUG("%d => %s %s %c (%d@%d)\n",
                        lc_idx,
                        bmdef->ciname[j],
                        bmdef->ciname[lc->lc],
                        (WPOS_NAME)[i],
                        bmdef->cd_tree[lc_idx].n_down,
                        bmdef->cd_tree[lc_idx].c.down);

                ++bmdef->cd_tree[ci_idx].n_down;
                ++lc_idx;
            }

            /* As above, so below. */
            if (bmdef->cd_tree[ci_idx].n_down == 0)
                bmdef->cd_tree[ci_idx].c.pid = -1;
            E_DEBUG("%d => %d=%s (%d@%d)\n",
                    ci_idx, j, bmdef->ciname[j],
                    bmdef->cd_tree[ci_idx].n_down,
                    bmdef->cd_tree[ci_idx].c.down);

            ++ci_idx;
        }
    }
    return bmdef->cd_tree;
}

bin_mdef_t *
bin_mdef_read_text(cmd_ln_t *config, const char *filename)
{
    bin_mdef_t *bmdef;
    mdef_t *mdef;
    int i, nchars, cionly;

    (void)config;

    cionly = (config == NULL) ? FALSE : cmd_ln_boolean_r(config, "-cionly");
    if ((mdef = mdef_init((char *) filename, cionly)) == NULL)
        return NULL;

    /* Enforce some limits.  */
    if (mdef->n_sen > BAD_SENID) {
        E_ERROR("Number of senones exceeds limit: %d > %d\n",
                mdef->n_sen, BAD_SENID);
        mdef_free(mdef);
        return NULL;
    }
    if (mdef->n_sseq > BAD_SSID) {
        E_ERROR("Number of senone sequences exceeds limit: %d > %d\n",
                mdef->n_sseq, BAD_SSID);
        mdef_free(mdef);
        return NULL;
    }
    /* We use uint8 for ciphones */
    if (mdef->n_ciphone > 255) {
        E_ERROR("Number of phones exceeds limit: %d > %d\n",
                mdef->n_ciphone, 255);
        mdef_free(mdef);
        return NULL;
    }

    bmdef = ckd_calloc(1, sizeof(*bmdef));
    bmdef->refcnt = 1;

    /* Easy stuff.  The mdef.c code has done the heavy lifting for us. */
    bmdef->n_ciphone = mdef->n_ciphone;
    bmdef->n_phone = mdef->n_phone;
    bmdef->n_emit_state = mdef->n_emit_state;
    bmdef->n_ci_sen = mdef->n_ci_sen;
    bmdef->n_sen = mdef->n_sen;
    bmdef->n_tmat = mdef->n_tmat;
    bmdef->n_sseq = mdef->n_sseq;
    bmdef->sseq = mdef->sseq;
    bmdef->cd2cisen = mdef->cd2cisen;
    bmdef->sen2cimap = mdef->sen2cimap;
    bmdef->n_ctx = 3;           /* Triphones only. */
    bmdef->sil = mdef->sil;
    mdef->sseq = NULL;          /* We are taking over this one. */
    mdef->cd2cisen = NULL;      /* And this one. */
    mdef->sen2cimap = NULL;     /* And this one. */

    /* Get the phone names.  If they are not sorted
     * ASCII-betically then we are in a world of hurt and
     * therefore will simply refuse to continue. */
    bmdef->ciname = ckd_calloc(bmdef->n_ciphone, sizeof(*bmdef->ciname));
    nchars = 0;
    for (i = 0; i < bmdef->n_ciphone; ++i)
        nchars += (int)strlen(mdef->ciphone[i].name) + 1;
    bmdef->ciname[0] = ckd_calloc(nchars, 1);
    strcpy(bmdef->ciname[0], mdef->ciphone[0].name);
    for (i = 1; i < bmdef->n_ciphone; ++i) {
        assert(i > 0); /* No reason to imagine it wouldn't be, but... */
        bmdef->ciname[i] =
            bmdef->ciname[i - 1] + strlen(bmdef->ciname[i - 1]) + 1;
        strcpy(bmdef->ciname[i], mdef->ciphone[i].name);
        if (strcmp(bmdef->ciname[i - 1], bmdef->ciname[i]) > 0) {
            /* FIXME: there should be a solution to this, actually. */
            E_ERROR("Phone names are not in sorted order, sorry.");
            bin_mdef_free(bmdef);
            mdef_free(mdef);
            return NULL;
        }
    }

    /* Copy over phone information. */
    bmdef->phone = ckd_calloc(bmdef->n_phone, sizeof(*bmdef->phone));
    for (i = 0; i < mdef->n_phone; ++i) {
        bmdef->phone[i].ssid = mdef->phone[i].ssid;
        bmdef->phone[i].tmat = mdef->phone[i].tmat;
        if (i < bmdef->n_ciphone) {
            bmdef->phone[i].info.ci.filler = mdef->ciphone[i].filler;
        }
        else {
            bmdef->phone[i].info.cd.wpos = mdef->phone[i].wpos;
            bmdef->phone[i].info.cd.ctx[0] = (uint8)mdef->phone[i].ci;
            bmdef->phone[i].info.cd.ctx[1] = (uint8)mdef->phone[i].lc;
            bmdef->phone[i].info.cd.ctx[2] = (uint8)mdef->phone[i].rc;
        }
    }

    /* If there are no CD phones there is no cdtree */
    if (mdef->n_phone == mdef->n_ciphone) {
        E_INFO("No CD phones found, will not build CD tree\n");
        bmdef->cd_tree = NULL;
    }
    else {
        build_cd_tree_from_mdef(bmdef, mdef);
    }
    mdef_free(mdef);

    bmdef->alloc_mode = BIN_MDEF_FROM_TEXT;
    return bmdef;
}

bin_mdef_t *
bin_mdef_retain(bin_mdef_t *m)
{
    ++m->refcnt;
    return m;
}

int
bin_mdef_free(bin_mdef_t * m)
{
    if (m == NULL)
        return 0;
    if (--m->refcnt > 0)
        return m->refcnt;

    switch (m->alloc_mode) {
    case BIN_MDEF_FROM_TEXT:
        ckd_free(m->ciname[0]);
        ckd_free(m->sseq[0]);
        ckd_free(m->phone);
        if (m->cd_tree)
            ckd_free(m->cd_tree);
        break;
    case BIN_MDEF_IN_MEMORY:
        ckd_free(m->ciname[0]);
        break;
    case BIN_MDEF_ON_DISK:
        break;
    }
    s3file_free(m->filemap);
    ckd_free(m->cd2cisen);
    ckd_free(m->sen2cimap);
    ckd_free(m->ciname);
    ckd_free(m->sseq);
    ckd_free(m);
    return 0;
}

bin_mdef_t *
bin_mdef_read(cmd_ln_t *config, const char *filename)
{
    bin_mdef_t *m;
    s3file_t *s;
    int cionly;

    /* Try to read it as text first. */
    if ((m = bin_mdef_read_text(config, filename)) != NULL)
        return m;
    E_INFO("Reading binary model definition: %s\n", filename);
    if ((s = s3file_map_file(filename)) == NULL) {
        E_ERROR_SYSTEM("Failed to open model definition file '%s' for reading",
                       filename);
        return NULL;
    }

    cionly = (config == NULL) ? FALSE : cmd_ln_boolean_r(config, "-cionly");
    m = bin_mdef_read_s3file(s, cionly);
    s3file_free(s);
    return m;
}

EXPORT bin_mdef_t *
bin_mdef_read_s3file(s3file_t *s, int cionly)
{
    size_t tree_start;
    int32 val, i;
    int32 *sseq_size;
    bin_mdef_t *m = NULL;

    if (s3file_get(&val, 4, 1, s) != 1) {
        E_ERROR("Failed to read byte-order marker\n");
        goto error_out;
    }
    if (val == BIN_MDEF_OTHER_ENDIAN) {
        E_INFO("Must byte-swap\n");
        s->do_swap = 1;
    }
    if (s3file_get(&val, 4, 1, s) != 1) {
        E_ERROR("Failed to read version\n");
        goto error_out;
    }
    if (val > BIN_MDEF_FORMAT_VERSION) {
        E_ERROR("File format version %d is newer than library\n",
                val);
        goto error_out;
    }
    if (s3file_get(&val, 4, 1, s) != 1) {
        E_ERROR("Failed to read header length\n");
        goto error_out;
    }
    /* Skip format descriptor. */
    s->ptr += val;
    if (s->ptr > s->end) {
        E_ERROR("Format descriptor truncated\n");
        goto error_out;
    }

    /* Finally allocate it. */
    m = ckd_calloc(1, sizeof(*m));
    m->refcnt = 1;

#define FREAD_SWAP32_CHK(dest)                  \
    if (s3file_get((dest), 4, 1, s) != 1) {     \
        E_ERROR("Failed to read %s\n", #dest);  \
        goto error_out;                         \
    }
    FREAD_SWAP32_CHK(&m->n_ciphone);
    FREAD_SWAP32_CHK(&m->n_phone);
    FREAD_SWAP32_CHK(&m->n_emit_state);
    FREAD_SWAP32_CHK(&m->n_ci_sen);
    FREAD_SWAP32_CHK(&m->n_sen);
    FREAD_SWAP32_CHK(&m->n_tmat);
    FREAD_SWAP32_CHK(&m->n_sseq);
    FREAD_SWAP32_CHK(&m->n_ctx);
    FREAD_SWAP32_CHK(&m->n_cd_tree);
    FREAD_SWAP32_CHK(&m->sil);

    /* CI names are first in the file. */
    m->ciname = ckd_calloc(m->n_ciphone, sizeof(*m->ciname));

    if (s->do_swap) {
        size_t rv, mdef_data_size = s->end - s->ptr;

        E_WARN("mdef is other-endian.  Will copy data.\n");
        /* Copy stuff so we can swap it. */
        m->alloc_mode = BIN_MDEF_IN_MEMORY;
        m->ciname[0] = ckd_malloc(mdef_data_size);
        if ((rv = s3file_get(m->ciname[0], 1, mdef_data_size, s)) != mdef_data_size) {
            E_FATAL("Failed to read %ld bytes of data: got %ld\n",
                    mdef_data_size, rv);
            goto error_out;
        }
    }
    else {
        /* Get the base pointer from the memory map. */
        m->ciname[0] = (char *)s->ptr; /* discard const, we know what we're doing */
        /* Success! */
        m->alloc_mode = BIN_MDEF_ON_DISK;
        m->filemap = s3file_retain(s);
    }

    for (i = 1; i < m->n_ciphone; ++i) {
        m->ciname[i] = m->ciname[i - 1] + strlen(m->ciname[i - 1]) + 1;
        if (m->alloc_mode == BIN_MDEF_ON_DISK
            && m->ciname[i] > s->end) {
            E_ERROR("ciname truncated!\n");
            goto error_out;
        }
    }

    /* Skip past the padding. */
    tree_start =
        m->ciname[i - 1] + strlen(m->ciname[i - 1]) + 1 - m->ciname[0];
    tree_start = (tree_start + 3) & ~3;
    m->cd_tree = (cd_tree_t *) (m->ciname[0] + tree_start);
    if (m->alloc_mode == BIN_MDEF_ON_DISK
        && (m->cd_tree + m->n_cd_tree) > (cd_tree_t *)s->end) {
        E_ERROR("cd_tree truncated!\n");
        goto error_out;
    }
    if (s->do_swap) {
        for (i = 0; i < m->n_cd_tree; ++i) {
            SWAP_INT16(&m->cd_tree[i].ctx);
            SWAP_INT16(&m->cd_tree[i].n_down);
            SWAP_INT32(&m->cd_tree[i].c.down);
        }
    }
    m->phone = (mdef_entry_t *) (m->cd_tree + m->n_cd_tree);
    if (m->alloc_mode == BIN_MDEF_ON_DISK
        && (m->phone + m->n_phone) > (mdef_entry_t *)s->end) {
        E_ERROR("phone truncated!\n");
        goto error_out;
    }
    if (s->do_swap) {
        for (i = 0; i < m->n_phone; ++i) {
            SWAP_INT32(&m->phone[i].ssid);
            SWAP_INT32(&m->phone[i].tmat);
        }
    }
    sseq_size = (int32 *) (m->phone + m->n_phone);
    if (m->alloc_mode == BIN_MDEF_ON_DISK
        && (sseq_size + 1) > (int32 *)s->end) {
        E_ERROR("sseq_size truncated!\n");
        goto error_out;
    }
    if (s->do_swap)
        SWAP_INT32(sseq_size);
    m->sseq = ckd_calloc(m->n_sseq, sizeof(*m->sseq));
    m->sseq[0] = (uint16 *) (sseq_size + 1);
    if (m->alloc_mode == BIN_MDEF_ON_DISK
        && (m->sseq[0] + *sseq_size) > (uint16 *)s->end) {
        E_ERROR("sseq truncated!\n");
        goto error_out;
    }
    if (s->do_swap) {
        for (i = 0; i < *sseq_size; ++i)
            SWAP_INT16(m->sseq[0] + i);
    }
    if (m->n_emit_state) {
        for (i = 1; i < m->n_sseq; ++i)
            m->sseq[i] = m->sseq[0] + i * m->n_emit_state;
    }
    else {
        m->sseq_len = (uint8 *) (m->sseq[0] + *sseq_size);
        if (m->alloc_mode == BIN_MDEF_ON_DISK
            && (m->sseq_len + m->n_sseq) > (uint8 *)s->end) {
            E_ERROR("sseq_len truncated!\n");
            goto error_out;
        }
        for (i = 1; i < m->n_sseq; ++i)
            m->sseq[i] = m->sseq[i - 1] + m->sseq_len[i - 1];
    }

    /* Now build the CD-to-CI mappings using the senone sequences.
     * This is the only really accurate way to do it, though it is
     * still inaccurate in the case of heterogeneous topologies or
     * cross-state tying. */
    m->cd2cisen = (int16 *) ckd_malloc(m->n_sen * sizeof(*m->cd2cisen));
    m->sen2cimap = (int16 *) ckd_malloc(m->n_sen * sizeof(*m->sen2cimap));

    /* Default mappings (identity, none) */
    for (i = 0; i < m->n_ci_sen; ++i)
        m->cd2cisen[i] = i;
    for (; i < m->n_sen; ++i)
        m->cd2cisen[i] = -1;
    for (i = 0; i < m->n_sen; ++i)
        m->sen2cimap[i] = -1;
    for (i = 0; i < m->n_phone; ++i) {
        int32 j, ssid = m->phone[i].ssid;

        for (j = 0; j < bin_mdef_n_emit_state_phone(m, i); ++j) {
            int s = bin_mdef_sseq2sen(m, ssid, j);
            int ci = bin_mdef_pid2ci(m, i);
            /* Take the first one and warn if we have cross-state tying. */
            if (m->sen2cimap[s] == -1)
                m->sen2cimap[s] = ci;
            if (m->sen2cimap[s] != ci)
                E_WARN
                    ("Senone %d is shared between multiple base phones\n",
                     s);

            if (j > bin_mdef_n_emit_state_phone(m, ci))
                E_WARN("CD phone %d has fewer states than CI phone %d\n",
                       i, ci);
            else
                m->cd2cisen[s] =
                    bin_mdef_sseq2sen(m, m->phone[ci].ssid, j);
        }
    }

    /* Set the silence phone. */
    m->sil = bin_mdef_ciphone_id(m, S3_SILENCE_CIPHONE);

    /* Now that we have scanned the whole file, enforce CI-only safely
     * by simply removing the cd_tree (means that we did some extraneous
     * scanning and byteswapping of senone sequences but that's ok) */
    if (cionly) {
        m->cd_tree = NULL;
        E_INFO
            ("%d CI-phone, %d CD-phone, %d emitstate/phone, %d CI-sen, %d Sen, %d Sen-Seq\n",
             m->n_ciphone, 0, m->n_emit_state,
             m->n_ci_sen, m->n_ci_sen, m->n_ciphone);
    }
    else {
        E_INFO
            ("%d CI-phone, %d CD-phone, %d emitstate/phone, %d CI-sen, %d Sen, %d Sen-Seq\n",
             m->n_ciphone, m->n_phone - m->n_ciphone, m->n_emit_state,
             m->n_ci_sen, m->n_sen, m->n_sseq);
    }
    
    return m;
 error_out:
    bin_mdef_free(m);
    return NULL;
}

int
bin_mdef_ciphone_id(bin_mdef_t * m, const char *ciphone)
{
    int low, mid, high;

    /* Exact binary search on m->ciphone */
    low = 0;
    high = m->n_ciphone;
    while (low < high) {
        int c;

        mid = (low + high) / 2;
        c = strcmp(ciphone, m->ciname[mid]);
        if (c == 0)
            return mid;
        else if (c > 0)
            low = mid + 1;
        else
            high = mid;
    }
    return -1;
}

int
bin_mdef_ciphone_id_nocase(bin_mdef_t * m, const char *ciphone)
{
    int low, mid, high;

    /* Exact binary search on m->ciphone */
    low = 0;
    high = m->n_ciphone;
    while (low < high) {
        int c;

        mid = (low + high) / 2;
        c = strcmp_nocase(ciphone, m->ciname[mid]);
        if (c == 0)
            return mid;
        else if (c > 0)
            low = mid + 1;
        else
            high = mid;
    }
    return -1;
}

const char *
bin_mdef_ciphone_str(bin_mdef_t * m, int32 ci)
{
    assert(m != NULL);
    assert(ci < m->n_ciphone);
    return m->ciname[ci];
}

int
bin_mdef_phone_id(bin_mdef_t * m, int32 ci, int32 lc, int32 rc, word_posn_t wpos)
{
    cd_tree_t *cd_tree;
    int level, max;
    int16 ctx[4];

    assert(m);

    /* CI phone requested, CI phone returned. */
    if (lc < 0 && rc < 0 && wpos == WORD_POSN_UNDEFINED)
        return ci;

    /* Exact match is impossible in these cases. */
    if (m->cd_tree == NULL || lc < 0 || rc < 0 || wpos == WORD_POSN_UNDEFINED)
        return -1;

    assert((ci >= 0) && (ci < m->n_ciphone));
    assert((lc >= 0) && (lc < m->n_ciphone));
    assert((rc >= 0) && (rc < m->n_ciphone));
    assert((wpos >= 0) && (wpos < N_WORD_POSN));

    /* Create a context list, mapping fillers to silence. */
    ctx[0] = wpos;
    ctx[1] = ci;
    ctx[2] = (m->sil >= 0
              && m->phone[lc].info.ci.filler) ? m->sil : lc;
    ctx[3] = (m->sil >= 0
              && m->phone[rc].info.ci.filler) ? m->sil : rc;

    /* Walk down the cd_tree. */
    cd_tree = m->cd_tree;
    level = 0;                  /* What level we are on. */
    max = N_WORD_POSN;          /* Number of nodes on this level. */
    while (level < 4) {
        int i;

        E_DEBUG("Looking for context %d=%s in %d at %d\n",
                ctx[level], m->ciname[ctx[level]],
                max, cd_tree - m->cd_tree);
        for (i = 0; i < max; ++i) {
            E_DEBUG("Look at context %d=%s at %d\n",
                    cd_tree[i].ctx,
                    m->ciname[cd_tree[i].ctx], cd_tree + i - m->cd_tree);
            if (cd_tree[i].ctx == ctx[level])
                break;
        }
        if (i == max)
            return -1;
        E_DEBUG("Found context %d=%s at %d, n_down=%d, down=%d\n",
                ctx[level], m->ciname[ctx[level]],
                cd_tree + i - m->cd_tree,
                cd_tree[i].n_down, cd_tree[i].c.down);
        /* Leaf node, stop here. */
        if (cd_tree[i].n_down == 0)
            return cd_tree[i].c.pid;

        /* Go down one level. */
        max = cd_tree[i].n_down;
        cd_tree = m->cd_tree + cd_tree[i].c.down;
        ++level;
    }
    /* We probably shouldn't get here, but we failed in any case. */
    return -1;
}

int
bin_mdef_phone_id_nearest(bin_mdef_t * m, int32 b, int32 l, int32 r, word_posn_t pos)
{
    word_posn_t tmppos;
    int p;

    /* In the future, we might back off when context is not available,
     * but for now we'll just return the CI phone. */
    if (l < 0 || r < 0)
        return b;

    p = bin_mdef_phone_id(m, b, l, r, pos);
    if (p >= 0)
        return p;

    /* Exact triphone not found; backoff to other word positions */
    for (tmppos = 0; tmppos < N_WORD_POSN; tmppos++) {
        if (tmppos != pos) {
            p = bin_mdef_phone_id(m, b, l, r, tmppos);
            if (p >= 0)
                return p;
        }
    }

    /* Nothing yet; backoff to silence phone if non-silence filler context */
    /* In addition, backoff to silence phone on left/right if in beginning/end position */
    if (m->sil >= 0) {
        int newl = l, newr = r;
        if (m->phone[(int)l].info.ci.filler
            || pos == WORD_POSN_BEGIN || pos == WORD_POSN_SINGLE)
            newl = m->sil;
        if (m->phone[(int)r].info.ci.filler
            || pos == WORD_POSN_END || pos == WORD_POSN_SINGLE)
            newr = m->sil;
        if ((newl != l) || (newr != r)) {
            p = bin_mdef_phone_id(m, b, newl, newr, pos);
            if (p >= 0)
                return p;

            for (tmppos = 0; tmppos < N_WORD_POSN; tmppos++) {
                if (tmppos != pos) {
                    p = bin_mdef_phone_id(m, b, newl, newr, tmppos);
                    if (p >= 0)
                        return p;
                }
            }
        }
    }

    /* Nothing yet; backoff to base phone */
    return b;
}

int
bin_mdef_phone_str(bin_mdef_t * m, int pid, char *buf)
{
    char *wpos_name;

    assert(m);
    assert((pid >= 0) && (pid < m->n_phone));
    wpos_name = WPOS_NAME;

    buf[0] = '\0';
    if (pid < m->n_ciphone)
        sprintf(buf, "%s", bin_mdef_ciphone_str(m, pid));
    else {
        sprintf(buf, "%s %s %s %c",
                bin_mdef_ciphone_str(m, m->phone[pid].info.cd.ctx[0]),
                bin_mdef_ciphone_str(m, m->phone[pid].info.cd.ctx[1]),
                bin_mdef_ciphone_str(m, m->phone[pid].info.cd.ctx[2]),
                wpos_name[m->phone[pid].info.cd.wpos]);
    }
    return 0;
}
