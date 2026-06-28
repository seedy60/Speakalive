# Speakalive

At the speed of light and with the spirit of the community, the Speakonia phoenix flies again

Speakalive is a lightweight, fully accessible text-to-speech program for Windows that puts blind and visually impaired users first. You type or paste some text, pick a voice, and Speakalive reads it aloud or saves it to an audio file. It features a graphical user interface (GUI) that is easy to drive from the keyboard with on-screen elements that are clearly labelled for screen readers such as [NVDA](https://nvaccess.org/about-nvda/) and [JAWS](https://www.freedomscientific.com/products/software/jaws/). A dark mode that follows your system, and word-by-word follow-along highlighting are all included. The whole program is a single, self-contained executable of 49 kilobytes that runs on every version of Windows from Windows 2000 through Windows 11, with no runtime or installer to fight with.

Speakalive aims to be a revival of Speakonia by CFS-Technologies, the little program that served as many people's gateway into creating text-to-speech content, with notable examples being [Thunderbirds101](https://www.youtube.com/Thunderbirds101) and AT88TV. It is also inspired by [Balabolka](https://www.cross-plus-a.com/balabolka.htm).

## Versioning

Speakalive is a retro style program through and through. To show commitment to the bit, releases have a 2002.x.x version number.

## Speakalive features

Speakalive speaks your text through whichever speech engines are installed on your computer: SAPI 4, SAPI 5, and the modern Windows OneCore voices on Windows 10 and 11. Each engine gets its own tab, and only the engines you actually have are shown, so the program automatically fits the machine it is running on. You can choose any installed voice, give it a short friendly name of your own, adjust the speaking rate and pitch (and the volume on SAPI 5), and start, pause, and stop the speech with single keypresses. You can save what you hear to a WAV or MP3 file in mono or stereo, and save the text you have written to a plain text file. Speakalive understands SAPI 4 control tags and SAPI 5 XML / SSML markup, so you can fine-tune pronunciation, emphasis, and timing right inside your text. A dark mode follows your Windows theme on Windows 10 and 11 and can be switched on by hand on older systems.

## Running Speakalive

### Compiled

Speakalive is a single, portable `Speakalive.exe` with no dependencies beyond the operating system itself, not even the Visual C++ runtime. Grab `Speakalive.exe` from the releases page (or build it yourself, see Compiling below), put it wherever you like, and run it. There is nothing to install. The same file runs on Windows 2000, XP, Vista, 7, 8, 8.1, 10, and 11.

* On Windows 10 and 11 you get all three engines: SAPI 4 (if installed), SAPI 5, and OneCore.
* On Windows 8.1 down to Windows XP you get SAPI 4 (if installed) and SAPI 5.
* On Windows 2000 you get SAPI 4, plus SAPI 5 if the SAPI 5.1 redistributable has been installed.

### from source

Speakalive is written in C using the native Win32 API. To build it you need Visual Studio (any edition with the C++ workload) and the Windows SDK. For the SAPI 4 engine you also need the Microsoft Speech SDK 4 installed at `C:\Program Files (x86)\Microsoft Speech SDK`. See Compiling below for the one-line build command.

## Choosing a speech engine and voice

When Speakalive opens, the engines available on your computer are shown as tabs across the top, E.G. SAPI 4, SAPI 5, and OneCore. SAPI 5 is selected by default when it is present.

1. Move to the tab row and use the Left and Right arrow keys to choose an engine, or press Control + Tab and Control + Shift + Tab to step to the next or previous engine from anywhere in the window.
2. Tab to the Voice list and use the arrow keys to choose one of that engine's installed voices.

The voice list, the sliders, and the available features update to match the engine you choose. Volume, for example, is offered only on SAPI 5.

## Renaming voices

The names speech engines give their voices can be long and unfriendly, such as "Adult Female #1, American English (TruVoice)". Speakalive lets you give any voice a short name of your own.

1. Select the voice you want to rename in the Voice list.
2. Choose Speech then Rename Voice, or press Control + R.
3. Type a friendly name, E.G. "Bridget", and press OK. Leave the box blank to go back to the voice's original name.

From then on the friendly name is what appears in the Voice list, and the list re-sorts by it. Your custom names are remembered between sessions and stored per voice, so picking a renamed voice still selects exactly the right one.

## Speaking text

1. Tab to the large Text to speak box and type or paste your text. Press Enter for new lines; press Tab to move on to the next control.
2. Press F5 to start speaking.
3. Press F6 to pause, and F6 again to resume. If nothing is speaking, F6 starts speaking.
4. Press F7 to stop.

You can also use the Speech menu, which lists every action together with its shortcut.

## Adjusting rate, pitch and volume

Below the text box are sliders for Rate and Pitch, and on SAPI 5, Volume. Tab to a slider and use the arrow keys, Page Up / Page Down, or Home / End to change it. The current value is shown beside each slider. Rate and Pitch read out as a signed number where 0 is the engine's normal setting; Volume reads as a percentage. Press the Reset Sliders button (Alt + E), or choose Speech then Reset Sliders, to put rate and pitch back to normal and volume back to 100 percent.

## Markup: control tags and XML

Speakalive can pass speech markup straight through to the engine so you can control pronunciation, emphasis, pitch, and timing inside your text. This is controlled by Speech then Speak as XML / SSML, which is on by default.

* SAPI 5 uses XML / SSML, E.G. `Hello <emph>there</emph>. <rate speed="-3"/>now a little slower.`
* OneCore uses SSML, E.G. `<speak version='1.0' xml:lang='en-US'><prosody rate='slow'>Hello</prosody></speak>`
* SAPI 4 uses its own control tags, E.G. `\Spd=120\ Hello \Pit=80\ now lower.` SAPI 4 control tags are always honoured.

When Speak as XML / SSML is on, Speakalive checks your markup before it speaks and, if something is wrong, tells you exactly what: an unclosed tag, a missing or extra quote mark, mismatched tags, or a `<` where a `>` was meant. You get a clear message naming the problem tag instead of a passage that silently refuses to speak.

With Speak as XML / SSML turned off, your text is read out exactly as written and any special characters are spoken literally.

## Saving audio

To save spoken audio to a file, choose File then Save to Audio File, or press Control + Shift + S.
Pick a location and filename, and choose WAV or MP3 from the file type box (or just type the extension you want).
WAV always works on every engine. MP3 needs an MP3 encoder: Speakalive uses an installed MP3 ACM codec if you have one, otherwise it looks for `lame.exe` next to `Speakalive.exe` or on your `PATH`. Stock Windows ships only an MP3 decoder, so for MP3 output drop a copy of `lame.exe` beside the program. If no encoder is available Speakalive tells you and you can still save a WAV.
You can [download LAME here](https://thecubed.cc/files/lame3.99.5.zip). Simply extract lame.exe from the zip and put it inside the folder where speakalive.exe lives.

## Reading a web page

Like Speakonia, Speakalive can read a web page aloud.

1. Choose File then Open Web Page, or press Control + W.
2. Type or paste a web address into the box, E.G. `https://en.wikipedia.org/wiki/Text-to-speech`, and press Enter or the OK button. If you leave off the `http://`, Speakalive adds it for you.
3. Speakalive fetches the page in the background (so the window never freezes), strips out the menus, scripts, and styling, and drops the readable text into the Text to speak box.
4. Press F5 to hear it, or edit and save it like any other text.

The page is downloaded with the system's own internet settings, so it works through proxies and over HTTPS, on every Windows from 2000 up. If a page cannot be reached, Speakalive tells you so. The reader keeps the readable text and discards the page furniture; it is a practical text extractor rather than a full browser, so very script-heavy pages may come through with less of their content.

## Saving your text

To save the text you have written to a plain text file, choose File then Save Text, or press Control + S. Pick a location and filename in the file picker and Speakalive writes your text to a `.txt` file.

## Auto save

With Speakonia, Balabolka or pretty much any other TTS software out there, be it vintage or modern, you have no insurance when the program crashes or Windows decides to freeze on you.

Speakalive includes an automatic save feature that means your work is always protected. In the event of a program crash, blue screen of death, power outage, mutant spider invasion or some other unexplained event, relaunching Speakalive will trigger a dialog asking if you want to recover the file you were working on. Hit yes and your text will reappear exactly as you left it before that transformer near your house decided it didn't wanna be hear anymore.

Furthermore, if you quit the program without saving your text, you will be asked if you want to save before exiting, so you're protected from closing the program by accident.

## Dark mode

The View menu controls Speakalive's appearance with two items:

* Dark Mode turns the dark theme on or off by hand.
* Follow OS Dark Mode is a checkbox that makes Speakalive track your Windows light/dark setting automatically.

On Windows 10 and 11 Follow OS Dark Mode is on by default. Speakalive reads your Windows "apps use light or dark theme" setting when it starts, applies the matching theme, follows live changes to that setting, and gives the program a dark title bar. Choosing Dark Mode by hand turns automatic following off so your choice sticks; tick Follow OS Dark Mode again to hand control back to Windows. On Windows 8.1 down to Windows 2000 there is no such system setting, so Follow OS Dark Mode is greyed out and Dark Mode is a purely manual switch. The dark theme covers the window, the text box, the voice list, the sliders, the buttons, and the status bar; the menu bar and the engine-tab strip keep the system colours.

## Accessibility

Speakalive is built to be operated entirely from the keyboard and to read well under NVDA, JAWS, and Narrator.

* Every control is a standard Windows control, so each one exposes a proper name, role, value, and state to your screen reader. Each slider is named, and disabled sliders report their state.
* Every label carries an Alt + letter shortcut, E.G. Alt + S jumps to the text box and Alt + R to the Rate slider.
* Tab and Shift + Tab move through every control, including out of the multi-line text box, and Control + A selects all of the text in it.
* Current status, E.G. Speaking, Paused, or Audio saved, is reported in the status bar. The window title stays a plain "Speakalive" so it does not churn while speaking.
* When something cannot be done, E.G. a markup mistake or a voice that cannot speak the text, Speakalive shows a clear message that your screen reader reads aloud, rather than only an error beep.
* Speech then Highlight Spoken Word (off by default) selects each word in the text box as it is spoken. It is off by default because, with focus in the text box, each selection change would make a screen reader announce the highlighted word over the speech.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| F5 | Speak the text |
| F6 | Play / Pause (also starts speaking if idle) |
| F7 | Stop |
| Control + Tab | Next speech engine |
| Control + Shift + Tab | Previous speech engine |
| Control + R | Rename the selected voice |
| Control + W | Open and read a web page |
| Control + S | Save your text to a text file |
| Control + Shift + S | Save spoken audio to a WAV / MP3 file |
| Control + A | Select all text (in the text box) |
| Tab / Shift + Tab | Move between controls |
| Arrow keys | Adjust the focused slider, or switch the focused engine tab |
| Alt + letter | Jump to the labelled control (E.G. Alt + R for Rate) |
| Alt + F4 | Exit |

## Compiling

Speakalive builds with a single command from a Visual Studio environment:

```
build.bat
```

This produces `build\Speakalive.exe`.

### Setting the version and metadata

The program's version number and the details that show up in the executable's Properties then Details tab in Windows Explorer (description, company, copyright, and so on) are set in one place: a block of variables near the top of `build.bat`, just under the `setlocal` line. Edit those values, run `build.bat`, and the new metadata is baked into `Speakalive.exe`.

The variables are:

* `VER_MAJOR`, `VER_MINOR`, `VER_PATCH`, `VER_BUILD`: the four parts of the version number, E.G. 2, 1, 0, 0 for version 2.1.0.0. These set both the File version and the Product version.
* `PRODUCT_NAME`: the product name, E.G. Speakalive.
* `FILE_DESC`: the file description shown most prominently, E.G. Speakalive text-to-speech.
* `COMPANY`: the company name.
* `COPYRIGHT`: the legal copyright line.
* `INTERNAL_NAME`: the internal name.
* `ORIG_FILENAME`: the original file name, E.G. Speakalive.exe.
* `COMMENTS`: a free-form comment.

For example, to cut version 2.0.0.0 with your own description, change the top of `build.bat` to:

```
set VER_MAJOR=2
set VER_MINOR=0
set VER_PATCH=0
set VER_BUILD=0
set "PRODUCT_NAME=Speakalive"
set "FILE_DESC=My custom Speakalive build"
set "COMPANY=Your Name"
set "COPYRIGHT=Copyright 2026 Your Name"
```

then run `build.bat`. Keep the quotes around the text values, and avoid the characters `&`, `<`, `>`, `|`, and `^` inside them. On each build these variables are written into `src\verinfo.h` (a generated file), which the resource script reads, so you do not edit `verinfo.h` yourself.

To run on Windows 2000, the build does not link the Visual C++ runtime (which does not exist there). Instead it compiles 32-bit with `/NODEFAULTLIB`, supplies its own tiny runtime, patches the executable's subsystem to 5.0, and keeps the import table to DLLs that ship with Windows 2000. Anything newer, such as the WinRT APIs used for OneCore voices and the dark title bar, is loaded at run time, so the same executable still loads on Windows 2000 and simply leaves those features out where the system cannot provide them.