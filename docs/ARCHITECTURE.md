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
tests/            Tests of each layer's behaviour at port or pure-logic boundaries
```

Dependencies point this way:

```text
presentation -> application -> core
       |              ^
       v              |
infrastructure -------+
```

`application` does not reference `presentation` or concrete `infrastructure` types. The concrete
implementations are all created and injected by `MainWindowCoordinator`:

- `CoreProcess` → `ZjuConnectProcess`
- `SystemProxyBackend` → `PlatformSystemProxyBackend`
- `ProfileBackend` → `ProfileManager`
- `SecretStore` → `KeychainSecretStore` (only when the build includes QtKeychain)

Platform code reports failures to its caller as an `OperationStatus`. Only `presentation` shows
dialogs.

## Main flows

- `MainWindowCoordinator` is the composition root for the main window, and maintains the lifecycle
  link between the VPN and the system proxy.
- `ConnectionUiController` handles connecting, disconnecting, the system proxy button and showing
  errors. It also keeps the system proxy on only while the session is connected.
- `AuthDialogCoordinator` manages the login, sudo, captcha, TOTP and SSO dialogs.
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
- `ConnectionUiController` holds the connect flow (which credentials are needed, when the system
  proxy is on, how an error is worded). That is use-case logic and belongs in `application`, behind
  an interface for asking the user, so that a front end other than the dialogs can reuse it.
- `presentation` uses several `infrastructure` types directly instead of through ports:
  `ApplicationPaths`, `DeviceTrust`, `CoreExecutable`, `Privileges`, `SettingsProfileLoader`,
  and `ProfileManager` in the settings window.
- A raw `QSettings` pointer for the current profile is handed to the dialogs. `ProfileService`
  replaces that object when the profile changes, so every holder has to be told; profile changes
  are refused while a dialog that edits the profile is open.

Put new code where its reason to change lives: UI behaviour in `presentation`, use-case state in
`application`, platform or file I/O in `infrastructure`, and connection rules that can be verified
on their own in `core`. Do not reintroduce a general `utils` directory; create a shared module only
for code that spans several responsibilities and has no clear owner.
