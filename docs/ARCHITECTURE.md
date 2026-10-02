# Architecture

EZ4Connect uses a lightweight layered architecture. The presentation layer combines application
services and platform implementations through coordinators, so that `MainWindow` does not itself
manage the lifecycle of the core process, settings storage or the authentication windows.

## What each directory is for

```text
core/             Connection domain model and state machine; no dependency on the UI or platform code
application/      Use cases, sessions and the ports implemented by infrastructure
infrastructure/   QProcess, QSettings, system proxy, files, credential store, update check and other implementations
presentation/     Qt Widgets UI, dialogs and the coordinators for UI flows
cli/              The command-line client: terminal prompts and signal handling
tests/            Tests of each layer's behaviour at port or pure-logic boundaries
```

Dependencies point this way:

```text
presentation, cli -> application -> core
       |                   ^
       v                   |
infrastructure ------------+
```

`core`, `application` and `infrastructure` are built as one static library, `ez4connect_engine`,
which does not use Qt Widgets. The graphical app, the command-line client and the tests all link
it.

`application` does not reference `presentation` or concrete `infrastructure` types. The concrete
implementations are created and injected by each front end's composition root:
`MainWindowCoordinator` for the graphical app and `cli/main.cpp` for the command-line client.

- `CoreProcess` → `ZjuConnectProcess`
- `SystemProxyBackend` → `PlatformSystemProxyBackend`
- `ProfileBackend` → `ProfileManager`
- `SecretStore` → `KeychainSecretStore` (only when the build includes QtKeychain)
- `AuthPrompter` → `AuthDialogCoordinator` (dialogs) or `TerminalPrompter` (terminal)

Platform code reports failures to its caller as an `OperationStatus`. Only `presentation` shows
dialogs.

## Main flows

- `MainWindowCoordinator` is the composition root for the main window, and maintains the lifecycle
  link between the VPN and the system proxy.
- `ConnectionFlow` is the connect flow that every front end shares: which credentials a profile
  still needs, passing the core's prompts to the user and the answers back, keeping the system
  proxy on only while the session is connected, and what an error means. It asks the user through
  the `AuthPrompter` port and reports through signals.
- `ConnectionUiController` puts that flow on screen: buttons, message boxes and the tray
  notification.
- `AuthDialogCoordinator` answers the flow's questions with the login, sudo, captcha, TOTP and SSO
  dialogs. `TerminalPrompter` answers them on a terminal.
- `ConnectionSession` is responsible for the core process, reconnecting and the connection state,
  and does not depend on the concrete `QProcess` implementation.
- `ProfileService` holds the current profile context, and `SettingsMigrator` migrates between
  configuration versions.
- `ProfileSettings` is the single definition of every profile setting: its key, the value a new
  profile starts with, and the value assumed when the key is missing. It also decides whether a
  secret lives in the credential store or in the profile file.

## Where the code does not match this yet

The layering above is the direction, not yet the whole truth:

- `MainWindow` is still large. Besides the window itself it runs profile creation, renaming and
  deletion, the settings migration prompts, device trust and the tray.
- `presentation` uses several `infrastructure` types directly instead of through ports:
  `ApplicationPaths`, `DeviceTrust`, `Privileges`, and `ProfileManager` in the settings window.
- The command-line client opens a profile's settings file itself instead of going through
  `ProfileService`, because selecting a profile there also makes it the active one.
- A raw `QSettings` pointer for the current profile is handed to the dialogs. `ProfileService`
  replaces that object when the profile changes, so every holder has to be told; profile changes
  are refused while a dialog that edits the profile is open.

Put new code where its reason to change lives: UI behaviour in `presentation`, use-case state in
`application`, platform or file I/O in `infrastructure`, and connection rules that can be verified
on their own in `core`. Do not reintroduce a general `utils` directory; create a shared module only
for code that spans several responsibilities and has no clear owner.
