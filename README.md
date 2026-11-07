Very much a project in its infancy. GoPro cameras are terrible with telemetry (DashWare
sucks, even if you can get it working. Usually, you can't sync video to telemetry - even
if the GoPro caught it all... which it didn't...) and Garmin no longer makes action
cameras that are worth crap. I have their latest non-360 and its static and dynamic
range is terrible, and the picture quality is so poor that I suspect they're expanding
to 1920 from a lower sensor resolution. Yes, it's that bad.

I want to be able to use a GoPro to shoot, but I want telemetry to WORK. So I built my
own telemetry gathering hardware (which will also be on github in the best way I can make
that happen.) This allows me to not only completely control HOW my telemetry is rendered,
but also WHAT I add. The nuance there is that my hardware is wireless and allows me to
connect as many sensors to my glider as I like. In a way, this project is just a
jumping-off point to building a stereo variometer, or whatever other glider-specific
flight computer project I find might be useful. But.... before I can work out WHETHER a
computer feature will be handy, I have to collect the data, see it overlaid atop my
video, and use those data to determine how I'd present them in real time to the pilot.

This is, by no means, production-style C++ yet. I'm still very much in the evolutionary
process. If something survives tentative inclusion, it lives on. The decision to move
large parts of the system to singletons, for example, is only implemented as far as
was necessary to solve certain immediate problems as they came up. When it works, I'll
clean it up. IOW, start-up mode. And no, I have no intention of starting a company on
this thing.

One actual prerequisite, for real this time: FFmpeg. CamBot leans on it for all the
video decode/encode work, and it used to mean hand-building it via MSYS2, which was
an absolute nightmare to get right and an even bigger nightmare to get right *again*
on a new machine. Not anymore -- the build now fetches a prebuilt, NVENC-capable
Windows FFmpeg automatically the first time you build in Visual Studio (it is a
pre-build step, see tools/fetch_ffmpeg.ps1). You do not need to install or build
anything yourself; just let that first build take a little longer while it downloads
~75MB into third_party/ffmpeg. If you ever want to move to a newer FFmpeg release,
that script is the one and only place that needs updating.

Good luck getting it to compile. There's a lot missing. No, I'm not going to document
that stage until I get to that whole "clean up" thing.
