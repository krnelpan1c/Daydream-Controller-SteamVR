# Daydream controller SteamVR driver
This driver and app allows you to connect a Google Daydream controller to your PC and use it in SteamVR, appearing as a Vive wand. I mainly made this so I can use it as a media remote / navigation controller for when I just want to watch a movie in VR, as the Daydream controller is perfect since it's light, simple, and has a convenient volume rocker instead of having to open the SteamVR dashboard.
Dependencies: [https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170](Visual Studio C++ Runtime Redistributable (2017-2026))

Note: Requires Bluetooth adapter installed in PC. Not familiar with other HMDs, but the internal bluetooth on your HMD that the controllers use will probably not work with this.
-
Default controls:
- Touchpad - Vive touchpad
- Touchpad click - Trigger pull
- App button - Application menu button
- Home button - System button (opens SteamVR dashboard)
- Hold home button - Recenters controller in the direction of the HMD
- Vol up/down - Volume control for current desktop/HMD output device

I don't expect people to use it for actual VR game input since it's so limited, but maybe you could get something going through Steam Input. Plus, if you're using this driver it probably means you already have an HMD with *real* 6DoF controllers since this is **NOT** a way to stream VR games to your Daydream headset. If you want to use Daydream as a SteamVR headset, combine this driver with ALVR or use iVRy which natively supports Daydream and the controller (e.g. if you're using a Mirage Solo or Daydream-ready phone and want to use the Daydream software)

# Usage
Download the latest release Driver.zip and extract it to a more permanent location on your PC (e.g. not your desktop) then run the executable in the bin\win64 folder. Click install SteamVR driver, set your controller hand, then hold the home button to put your controller in pairing mode and wait for it to connect. Once connected, launch SteamVR. The app needs to run in the background but automatically minimizes to the system tray and will automatically close when SteamVR closes.

# Tip
I wouldn't recommend running the app while your regular HMD's controllers of the same hand are connected (e.g. right HMD controller connected at the same time as Daydream controller set to right hand), as this can cause your controllers to be finnicky with SteamVR. If you want to switch to your regular HMD's controllers, close the app, then pair your HMD's controllers. Switching other way around: disconnect HMD, open app, you get the picture.
