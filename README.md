![Daydream aurora with logo and SteamVR logo hero image](hero.webp)
# Daydream controller SteamVR driver
This driver and app allows you to connect a Google Daydream controller to your PC and use it in SteamVR, appearing as a Vive wand. I mainly made this so I can use it as a media remote / navigation controller for when I just want to watch a movie in VR, as the Daydream controller is perfect since it's light, simple, and has a convenient volume rocker instead of having to open the SteamVR dashboard.

Dependencies: [Visual Studio C++ Runtime (2017-2026)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)

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
Download the latest release Driver.zip and extract it to a more permanent location on your PC (e.g. not your desktop) then run the executable in the bin\win64 folder. Click Install SteamVR Driver, then start SteamVR. Press the Home button on your controller to wake it; the SteamVR driver pairs and connects it automatically. In the app, click the Assign button for the hand you want, then press a button on the controller to assign it to that hand (SteamVR must be running for this). Set your button mappings if the defaults don't suit you and click Save Mappings; changes apply immediately without restarting SteamVR. The app is only needed for configuration and can be closed afterwards.

# Tip
I wouldn't recommend using a Daydream controller while your regular HMD's controller of the same hand is connected (e.g. right HMD controller connected at the same time as Daydream controller set to right hand), as this can cause your controllers to be finnicky with SteamVR. If you want to switch to your regular HMD's controllers, turn off the daydream driver in SteamVR's Settings > Startup / Shutdown > Manage Add-ons (or clear the assignments in the app), then pair your HMD's controllers.
