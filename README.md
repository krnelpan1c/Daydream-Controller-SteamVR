# Daydream controller SteamVR driver
This driver and app allows you to use a Google Daydream controller as a SteamVR controller, appearing as a Vive wand. I mainly made this so I can use it as a media remote / navigation controller for when I just want to watch a movie in VR, as the Daydream controller is perfect since it's light, simple, and has a convenient volume rocker instead of having to open the SteamVR dashboard.

Note: Requires Bluetooth adapter installed in PC. Not familiar with other HMDs, but the internal bluetooth that your HMD and controllers use will probably not work with this.
-
Default controls:
- Touchpad - Vive touchpad
- Touchpad click - Trigger pull
- App button - Application menu button
- Home button - System button (opens SteamVR dashboard)

I don't expect people to use it for actual VR game input since it's so limited, but maybe you could get something going through Steam Input. Plus, if you're using this driver it probably means you already have an HMD with *real* 6DoF controllers since this is **NOT** a way to stream VR games to your Daydream headset.

# Usage
Download the latest release driver.zip and extract it to a more permanent location on your PC (e.g. not your desktop) then run the executable in the bin folder. Click install SteamVR driver, set your controller hand, and click Connect. Hold the home button to put your controller in pairing mode and wait for it to connect. Once connected, launch SteamVR. The app needs to run in the background but can be minimized to the system tray and will automatically close when SteamVR closes.
