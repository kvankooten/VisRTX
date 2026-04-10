# TSD USD Export Viewer

Interactive TSD viewer (`tsdUsdExportViewer`) with a **USD Export** panel. It loads the ANARI device named `usd`, syncs the TSD scene to it, and can serialize to local paths or Omniverse-style hosts depending on your USD device build and settings.

## Build

Build TSD with interactive applications enabled (default when TSD is enabled).

From the VisRTX tree, configure with `VISRTX_BUILD_TSD=ON`, then build the `tsdUsdExportViewer` target. The executable is emitted under your CMake binary directory (for example `tsdUsdExportViewer.exe` on Windows).

You need a matching ANARI **USD** device library available at runtime (same major ANARI API as TSD was built against), plus whatever OpenUSD and third-party DLLs that device depends on.

## Runtime environment

General pattern: prepend **your** install and dependency `bin` / `lib` directories to `PATH` (on Linux, `LD_LIBRARY_PATH` is the usual analogue) so the process can load the ANARI USD device, USD, ANARI SDK utilities, and any required runtimes (for example Intel TBB if your stack ships it separately).

### Example layout (placeholders)

Replace the bracketed segments with paths from your own build or install tree.

| Purpose | Typical locations to add to `PATH` |
|--------|-------------------------------------|
| Application and ANARI device install | `<your-install>/bin`, `<your-install>/lib` |
| OpenUSD (release or RelWithDebInfo, as built) | `<your-usd-build-or-install>/bin`, `<your-usd-build-or-install>/lib` |
| ANARI SDK (if not already covered) | `<anari-sdk-install>/bin` |
| Intel oneTBB redistributables (if required) | `<tbb-redist>/intel64/vc14` (Windows example) |

Concrete illustration (Windows `PATH` fragments only—adapt drive letters and folder names):

```text
<install-prefix>/lib;<install-prefix>/bin;<usd-prefix>/lib;<usd-prefix>/bin;<anari-sdk-prefix>/bin;<tbb-redist>/intel64/vc14;%PATH%
```

Keep **configuration consistent** (for example all RelWithDebInfo or all Release) across TSD, the USD device, and USD to avoid subtle load failures.

### OmniStorage (optional)

When using an OmniStorage-backed workflow, the USD device stack may read:

| Variable | Meaning |
|----------|---------|
| `OMNI_STORAGE_HOST` | Hostname or IP of the machine on which the OmniStorage service runs |
| `OMNI_STORAGE_PORT` | TCP port the service listens on (the Nodeport from `kubectl describe svc <storage-service-name>`) |
| `OMNI_STORAGE_ENABLED` | Non-zero to enable OmniStorage behavior (exact semantics depend on your USD device / kit version) |

Example (values are illustrative only):

```text
set OMNI_STORAGE_HOST=<storage-host>
set OMNI_STORAGE_PORT=<port>
set OMNI_STORAGE_ENABLED=1
```

The **USD Export** panel also exposes **Server URL** and **Output Folder** - these are the specific S3/Azure https URL of the storage backend (e.g. `https://<bucket name>.s3.us-west-1.amazonaws.com`) and an optional subfolder to which the files will be written via the OmniStorage service (the service requires the necessary access credentials to be set up in its .yaml).

### Omni write service (host)

Documentation for installing and running the Omni write service on the storage host will be added separately.

## Run

From a shell where `PATH` (and any other required variables) are set:

```text
tsdUsdExportViewer [options and scene arguments...]
```

Scene loading follows the same TSD conventions as other interactive apps: optional importer flags (for example `-gltf`, `-usd`, `-blank`) and file paths, or a saved state file. UI-specific flags include `--noDefaultLayout`, `--noDefaultRenderer`, and `--secondaryView` / `-sv` (see `tsd::ui::imgui::Application`).

After startup, open **USD Export**, configure the **Server URL** and **Output Folder** as needed, click **Enable USD Device**, then use **Sync Scene** (or enable **Auto-sync on scene change**) to export.
