# Third-party software

dcompile links against, and does not bundle, these libraries:

- **FFmpeg** (libavformat, libavcodec, libavutil, libswscale, libswresample), used to decode Bink video and audio. FFmpeg is
  licensed under the LGPL v2.1 or later, or the GPL, depending on how your copy was built. dcompile links to it dynamically
  through the Homebrew installation and distributes none of its code. If you distribute binaries of dcompile, check the license
  terms of the FFmpeg build you link against.
- **Apple system frameworks** (CoreFoundation, Cocoa, OpenGL, AudioToolbox, CoreAudio, IOKit, CommonCrypto).

The Ubisoft and RAD Game Tools libraries and executables the game ships with are not part of this project and are never
redistributed by it.
