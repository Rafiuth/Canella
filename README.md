# Canella - Audio visualiser for Spicetify

# How it works
Audio data is captured and partially processed (FFT) using platform dependend code in C++, and served through a localhost websocket server. The JS extension is based on [fullAppDisplay](https://github.com/khanhas/spicetify-cli/blob/master/Extensions/fullAppDisplay.js).

The audio server was intended to run along with spotify automatically via DLL hijacking, and maybe LD_PRELOAD on Linux.

# Demo
https://user-images.githubusercontent.com/53208252/141041827-2cb4e9ce-f6a5-4865-8f9e-470d6376d4d5.mp4

