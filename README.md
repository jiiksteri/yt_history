# yt_history: A YouTube Watch History browser server

Runs a http server on localhost rendering you a simple, if stupid
representation of your YouTube Watch History, available
[here](http://www.youtube.com/feed/history).

To access this information yt_history uses the Google Data APIs. The
program needs your OAuth2 authorization to operate.

## What good is this?

Not much. See [this G+ post](https://plus.google.com/106361938481599545441/posts/86QvofmxPes)
for details.

## Requirements

 * libevent 2.0.x: [http://libevent.org/](http://libevent.org/)
 * libssl from OpenSSL: [http://openssl.org/](http://openssl.org/)
 * libjson: [https://github.com/json-c/json-c/wiki](https://github.com/json-c/json-c/wiki)
 * libexpat: [http://expat.sourceforge.net/](http://expat.sourceforge.net/)

For unit test(s):

 * CUnit: [https://cunit.sourceforget.net](http://cunit.sourceforge.net)

For running the server you'll need Google Data API keys. See the
[Google Developers site](http://developers.google.com/) for more information.

## Building

    make

The makefile uses pkg-config for figuring out build flags.

## Running

Place your client id and client secret where yt_history can find them:

    $HOME/.yt_history/client_id
    $HOME/.yt_history/client_secret

Start the server:

    ./yt_history -p <listening_port> [ -v [ -v ] ... ]

If you do not specify a port, one will be allocated for you. The
listening address will be printed on the console.

Passing -v increases verbosity.

Point your browser at localhost. Your browser will be redirected to
Google for authorization. When the browser returns we show a somewhat
crude representation of your YouTube Watch History, unless the bugs get
to us before we get so far.

## But why?

Oh, no reason. Kittens.

## Can't you do this with, like, 5 seconds of JavaScript?

Probably. _I_, however, can't.

## Copyright

Copyright (c) 2012 Jukka Ollila

Published under the GNU General Public License, version 2.0. See the
LICENSE file for details.

