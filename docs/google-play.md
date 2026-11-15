# Publishing on Google Play

This guide takes a Rolo game from the Android export to a page in Google
Play: the account, the key, the forms, the testing Google asks for, and
updates afterwards.

**Read this first.** Rolo's Android export is experimental. What Rolo's
own checks cover is listed under [what has been tested](#what-has-been-tested);
**nobody has yet uploaded a Rolo game to Google Play**, so the first maker
who does is finding out for everyone. Please tell us how it went
([an issue](https://github.com/8dkj4fjrrs-cmyk/Rolo/issues) is fine),
above all what the pre-launch report said.

Google changes its rules and moves its menus about every year. The rules
below were checked against Google's own help pages in **November 2026**;
each part links to the page it came from, and where the two differ, Google's
page is right.

## The short version

| | |
|---|---|
| Cost | US$25, once, for the developer account. No yearly fee. |
| You need | To be 18 or older, a Google account, an identity document, a payment card, and **an Android phone or tablet** (a new personal account must show it has one). |
| How long | About **three weeks** at least for a first game on a new personal account: Google wants it tested by 12 people for 14 days before it may be published. |
| What you upload | `My Game.aab`, made by **File › Export Game… › Android** with *Sign with my own key* ticked. |

## 1. Before you start

- **Export once and try the game** ([Exporting, "For Android"](exporting.md#for-android)),
  on a real phone if you can. Rolo itself has only been tried in an
  emulator.
- **Choose the package name with care**, for example `com.adalovelace.moonfall`:
  something that is yours (your name, or a web address you own written
  backwards), then the game. Google Play keeps it for ever; a game cannot
  be given another one later. Rolo will not make the file for Google Play
  while the name starts with `com.example` or still has `yourname` in it.
- **Give the game its own icon** (a square PNG, 512 × 512, set in the
  export dialog).

## 2. The developer account

Sign up at [play.google.com/console](https://play.google.com/console).
Google's steps are on [its help page](https://support.google.com/googleplay/android-developer/answer/6112435).

- You choose between a **personal** account (yourself) and an
  **organisation** account (a registered company, which needs a D-U-N-S
  number). A hobbyist takes personal.
- Google checks who you are (an identity document, and a card in your
  name). The name you register is shown on your game's page.
- A new personal account must also prove it has an Android device, with
  the *Play Console* app installed on that device. An emulator will not
  do: if you only have a Mac and an iPhone, you need to borrow or buy an
  Android phone for this (it is also the best way to test your game).
- **Android developer verification**, a newer rule under which Android
  itself only installs apps registered by a verified developer, needs
  nothing extra from you for a game on Google Play: your Play Console
  account covers it, and Google registers Play games by itself. Your
  Console's home page shows the state. ([Google's page](https://developer.android.com/developer-verification))

## 3. The upload key, and Google's key

Two keys are involved, and it helps to know which is which
([Google's page on Play App Signing](https://support.google.com/googleplay/android-developer/answer/9842756)):

- The **app signing key** signs the game players install. Google makes it
  and keeps it when you upload your first file. You never hold it.
- The **upload key** is yours: the file Rolo made with **New Key…**, and
  its password. It proves to Google that an upload comes from you.

So:

- **Keep the key file and the password safe, with a copy of both
  somewhere else** (a USB stick in a drawer, a password manager). Not in
  the project's folder, and never in a public repository.
- **If you lose it, the game is not lost.** Google can replace an upload
  key: you make a new one in Rolo, and the account's owner asks for a
  reset in the Play Console (in the settings of *Play app signing*; Google
  asks for the new key's certificate, below). It is a nuisance, though: it
  takes days, and you cannot upload anything while you wait.
- If someone else gets hold of the key and its password, ask for the same
  reset.

The certificate Google asks for is made with Java's `keytool`, which
Android Studio brings (in Terminal; it asks for the key's password):

```
keytool -export -rfc -keystore "upload-key.jks" -alias upload -file upload_certificate.pem
```

Rolo never stores the password; you type it at each export for Google
Play.

## 4. Create the app in the Play Console

**Create app**, then: the name, the language, **Game**, and **Free** or
**Paid**. (A game published as free can never be made paid later.)

The Console then shows a list of things to set up. They come in three
groups: what the game is ([the forms](#5-the-forms)), how its page looks
([the store listing](#6-the-store-listing)) and the releases.

## 5. The forms

They are under **Policy and programs › App content**
([Google's page](https://support.google.com/googleplay/android-developer/answer/9859455)).
You answer for your own game, and Google holds you to the answers. What
follows is what is true of **Rolo's part**; your game's content is yours
to describe.

| Form | For a Rolo game |
|---|---|
| **Privacy policy** | Google asks for the address of a web page. A short, true one is enough ([an example](#a-privacy-policy-for-a-game-that-collects-nothing)). |
| **Ads** | *No.* Rolo cannot show any. |
| **App access** | *All functionality is available without special access*: there is nothing to sign in to. |
| **Content rating** | A questionnaire about what is in the game (violence, scary scenes, language, gambling...). Answer for your game; the age ratings follow from it. A game without a rating may not be published. |
| **Target audience** | The ages the game is made for. Choosing any group **under 13** puts the game under Google's stricter [Families rules](https://support.google.com/googleplay/android-developer/answer/9893335). Be truthful: Google also judges by how the game looks. |
| **Data safety** | See below. |
| The others (news, health, financial features, government...) | *No* / not applicable, for a game. |

**Data safety.** The form asks what the app *collects* (sends off the
phone) and *shares*. A game exported by Rolo:

- asks for **no permissions** and has no way to use the internet (the
  export has no networking in it; Rolo's checks fail if the app asks for
  any permission);
- contains no advertising, analytics or crash-reporting kit, and no
  Google Play services;
- keeps saved games and settings in the app's own folder on the phone.

So the honest answer to "Does your app collect or share any of the
required user data types?" is, for Rolo's part, **No**.

One thing to know when you answer: **saved games are included in
Android's own backup.** If the player has backups switched on, Android
copies the game's `saves` folder (only that) to the player's own Google
account, and brings it back on a new phone or after reinstalling. This is
the phone's own service: neither you nor Rolo ever receives those files.
We read Google's form as not counting this as collection by your app, but
the answers are yours to give, so read
[Google's explanation](https://support.google.com/googleplay/android-developer/answer/10787469)
of the terms once.

### A privacy policy for a game that collects nothing

Put a page like this on any website you control (your itch.io page, a
GitHub Pages site), with your own details in it:

> **Privacy policy for Moonfall**
>
> Moonfall does not collect, send or share any personal information. It
> does not use the internet, shows no advertising and contains no
> tracking.
>
> Your saved games are stored on your device. If you have Android's backup
> switched on, your device may keep a copy of them in your own Google
> account; we cannot see it.
>
> Questions: ada@example.org. Last changed: 15 November 2026.

Only write what is true of your game. This is an example, not legal
advice.

## 6. The store listing

Under **Grow users › Store presence › Main store listing**
([Google's page on the pictures](https://support.google.com/googleplay/android-developer/answer/1078870)):

| | |
|---|---|
| App name | Up to 30 characters. |
| Short description | Up to 80 characters. |
| Full description | Up to 4000 characters. |
| App icon | PNG, 512 × 512, at most 1024 KB. **Rolo writes it for you**: `Google Play icon (512).png` in the export folder, if the game has its own icon. |
| Feature graphic | A banner, 1024 × 500, JPEG or PNG without transparency. You draw this one. |
| Phone screenshots | At least 2. Take them from the game running on a phone, or in Android Studio's emulator (the camera button beside it). |

Also there: the category (a kind of game) and a contact e-mail address,
which is shown to everyone.

## 7. Releases: try, test, publish

Play has four "tracks". A new personal account has to go through them in
this order ([Google's page](https://support.google.com/googleplay/android-developer/answer/14151465)):

1. **Internal testing** (**Test and release › Testing › Internal testing**).
   Up to 100 people you name by e-mail address; an upload is with them
   within minutes, with no review. **Start here**: create a release,
   upload `My Game.aab`, add your own address as a tester, open the
   tester's link on your phone and install the game from Google Play. This
   is the moment you learn whether Google Play accepts Rolo's file.
2. **Closed testing.** The step Google insists on: **at least 12 testers,
   opted in for 14 days in a row**, before you may ask to publish.
   Internal testers do not count for this. Friends, family, a game-making
   community: ask them early, and ask them to stay opted in. The first
   closed release is reviewed by Google, which can take some days.
3. **Apply for production** (a button on the Console's Dashboard, once the
   14 days are done). Google asks how the test went and what you changed;
   an answer usually comes within a week.
4. **Production**: the game is in the store.

Organisation accounts, and personal accounts made before 13 November
2023, may publish without the closed test.

**The first upload** asks you to accept *Play App Signing* (say yes; see
[above](#3-the-upload-key-and-googles-key)).

### The pre-launch report

After an upload to a testing track, Google installs the game on a handful
of real phones, lets a robot tap around in it for a few minutes, and
writes down crashes, freezes, slow starts and screenshots: **Test and
release › Testing › Pre-launch report**. It can take an hour or more to
appear.

What to look at:

- **Stability.** Any crash or "not responding" is worth reporting to us,
  with the device's name.
- **The screenshots.** Does the first scene show on every phone? Is
  anything cut off by a camera notch or rounded corners?
- **Warnings you can expect.** The robot does not know how to play, so it
  may never leave your title screen. Accessibility warnings (small touch
  targets, low contrast, missing labels) are about ordinary apps'
  buttons; a game drawn by an engine gets some, and they do not stop a
  release.

**No pre-launch report of a Rolo game has been seen yet.** If you get
one, please share it.

## 8. Updates

- Export again and upload the new `.aab` as a new release.
- Every upload needs a **higher version code** than any before it, on
  every track. After each export for Google Play the editor raises the
  project's version code by one for the next time (the dialog says so;
  Undo takes it back). Google Play refuses a file whose code it has seen.
- The **version** (`1.0`, `1.1`...) is only what players read; change it
  when you like.
- Each year Google raises the Android version new uploads must be built
  for ("target API level"; since 31 August 2026 it is Android 16, level
  36, which is what Rolo builds for). When that moves, a newer Rolo will
  be needed to upload updates.
  ([Google's page](https://support.google.com/googleplay/android-developer/answer/11926878))

## What Rolo's file already takes care of

| Google Play asks for | In a Rolo export |
|---|---|
| An Android App Bundle (`.aab`), signed | Yes, with your upload key. |
| Target API level 36 | Yes. |
| 64-bit code | Yes (arm64 for phones, x86_64 for emulators and Chromebooks). |
| Memory pages of 16 KB | Yes; checked with every change. |
| Not debuggable | Yes. |
| A version code | From the project; raised after each export for Google Play. |

## What has been tested

On GitHub's machines (Linux), with every change to Rolo's Android parts,
using the Platformer Starter:

- The `.aab` is checked with **bundletool**, the program Google Play
  itself uses to turn a bundle into the apps phones download: it is
  well-formed, says the right package name, version code and target
  level, asks for no permissions and is not debuggable.
- bundletool then makes an app out of the bundle, as Google Play would,
  and that app is **played in an emulator** (Android 14).
- **Backup**: a saved game is backed up, the game removed and installed
  again, and the saved game comes back while other files do not (with the
  emulator's stand-in for Google's backup).

Not tested, by anyone, yet:

- An upload to Google Play, its review, and Play App Signing.
- The pre-launch report.
- A real phone.
- Backups to a real Google account.
