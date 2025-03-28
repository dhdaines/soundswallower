soundswallower JavaScript package
=================================

SoundSwallower can be called either from Python (see
:doc:`soundswallower` for API details) or from JavaScript.  In the
case of JavaScript, we use `Emscripten <https://www.emscripten.org>`_
to compile the C library into WebAssembly, which is loaded by a
JavaScript wrapper module.  This means that there are certain
idiosyncracies that must be taken into account when using the library,
mostly with respect to deployment and initialization.

Using SoundSwallower on the Web
-------------------------------

Since version 0.3.0, SoundSwallower's JavaScript API can be used
directly from a web page without any need to wrap it in a Web Worker.
You may still wish to do so if you are processing large blocks of data
or running on a slower machine.  Doing so is currently outside the
scope of this document.

Most of the methods of :js:class:`Decoder` are asynchronous.  This
means that you must either call them from within an asynchronous
function using ``await``, or use the ``Promise`` they return in the
usual manner.  If this means nothing to you, please consult
https://developer.mozilla.org/en-US/docs/Learn/JavaScript/Asynchronous.

This means that for older browsers you will need to use some kind of
magical incantation that "polyfills" (I think that's what the kids
call it) or transcodes or whatever so that it works right.  Also
outside the scope of this document, because there are approximately
59,000 of these, all of which claim to be THE BEST EVAR and I do not
know which one you should use.

By default, a narrow-bandwidth English acoustic model is loaded and
made available.  If you want to use a different one, just put it where
your web server can find it, then pass the relative URL to the
directory containing the model files using the `hmm` configuration
parameter and the URL of the dictionary using the `dict` parameter.
Here is an example, presuming that you have downloaded and unpacked
the Brazilian Portuguese model and dictionary from
https://sourceforge.net/projects/cmusphinx/files/Acoustic%20and%20Language%20Models/Portuguese/
and placed them under ``/model`` in your web server root:

.. code-block:: javascript

    // Avoid loading the default model
    const ssjs = { defaultModel: null };
    await require('soundswallower')(ssjs);
    const decoder = new ssjs.Decoder({hmm: "/model/cmusphinx-pt-br-5.2",
                                      dict: "/model/br-pt.dic"});
    await decoder.initialize();

Using SoundSwallower under Node.js
----------------------------------

Using SoundSwallower-JS in Node.js is mostly straightforward.  Here is
a fairly minimal example.  First you can record yourself saying some
digits (note that we record in 32-bit floating-point at 44.1kHz, which
is the default format for WebAudio and thus the default in
SoundSwallower-JS as well):

.. code-block:: console

   sox -c 1 -r 44100 -b 32 -e floating-point -d digits.raw

Now run this with ``node``:

.. code-block:: javascript

    (async () => { // Wrap everything in an async function call
	// Load the library and pre-load the default model
	const ssjs = await require("soundswallower")();
	const decoder = new ssjs.Decoder();
	// Initialization is asynchronous
	await decoder.initialize();
	const grammar = decoder.parse_jsgf(`#JSGF V1.0;
    grammar digits;
    public <digits> = <digit>*;
    <digit> = one | two | three | four | five | six | seven | eight
	| nine | ten | eleven;`); // It goes to eleven
	// Anything that changes decoder state is asynchronous
	await decoder.set_fsg(grammar);
	// We must manually release memory, because JavaScript
	// has no destructors, whose great idea was that?
	grammar.delete();
	// Default input is 16kHz, 32-bit floating-point PCM
	const fs = require("fs/promises");
	let pcm = await fs.readFile("digits.raw");
	// Start speech processing
	await decoder.start();
	// Takes a typed array, as returned by readFile
	await decoder.process(pcm);
	// Finalize speech processing
	await decoder.stop();
	// Get recognized text (NOTE: synchronous method)
	console.log(decoder.get_hyp());
	// Again we must manually release memory
	decoder.delete();
    })();


Decoder class
-------------

.. js:autoclass:: pre_soundswallower.Decoder
   :members:
   :short-name:

Config class
-------------

.. js:autoclass:: pre_soundswallower.Config
   :members:
   :short-name:

Functions
---------

.. js:autofunction:: pre_soundswallower.get_model_path
   :short-name:
