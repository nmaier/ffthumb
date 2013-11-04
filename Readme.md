ffthumb - Yet another ffmpeg thumbnail library
===

Overview
---

* Simpler than most other libraries.
* Will spit out (Windows) BMP.
* Coded against ffmpeg master.
* More accurate seeking than most other libraries.
  Instead of using just keyframes, which sucks especially for small videos
  which only got one, this `ffthumb` will actually seek to the nearest keyframe
  and then will try to seek to the nearest frame by decoding non-keyframes.
* More accurate seeking makes it better for analysis tasks, as keyframes and
  the keyframe interval are largely dependent on the producing software.

API
---

See `thumb.h`.

To make dynamic loading (`LoadLibrary` and/or `dl`) more pleasant, the library
init function `ffthumb_init()` will return a structure containing function
pointers to the other API functions.


License
---

The code is under MIT-License. See `LICENSE`.
By linking this library with ffmpeg, the resulting library will be likely LGPL,
GPL or non-free (undistributable) depending on the ffmpeg configuration. See
the ffmpeg documentation.

Building
---

See the `Makefile`. Up until now I only did some preliminary tests under Linux
and mingw-w64. Stuff might not work for you.

Status
---

The library is considered alpha grade so far. I'm not that familiar with the API
and might have messed something up, in particular memory leaks. As such you
shouldn't rely on the library in long running processes and it probably would be
a good idea to run this thing sandboxed.
