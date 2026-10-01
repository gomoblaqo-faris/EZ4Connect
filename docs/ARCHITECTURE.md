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

Put new code where its reason to change lives: UI behaviour in `presentation`, use-case state in
`application`, platform or file I/O in `infrastructure`, and connection rules that can be verified
on their own in `core`. Do not reintroduce a general `utils` directory; create a shared module only
for code that spans several responsibilities and has no clear owner.
