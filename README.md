# SMIT52 Backup Service

Windows service that periodically scans a configured directory and writes the matching files to a standalone ZIP archive. It uses the ZIP **Store** method (no compression); the archive is assembled by the program itself and needs no 7-Zip or other archiver.

## Build

Build with a Windows C++23 toolchain and CMake:

```powershell
cmake --preset win-clang
cmake --build out/build/win-clang --config Debug
```

The service uses only the Windows Service Control Manager API and the C++ standard library. The ZIP writer supports classic ZIP limits: each file and the complete archive must be smaller than 4 GiB, with at most 65,535 files.

## Configuration

Create `SMIT52.ini` in the same directory as `SMIT52.exe`, before installing or starting the service. Save it as UTF-8. For example:

```ini
source_directory=\\localhost\C$\Data\Documents
archive_path=\\localhost\C$\Backups\documents.zip
patterns=*.docx;*.pdf;important.txt
interval_seconds=60
```

`source_directory` is scanned recursively. `archive_path` is replaced with a fresh snapshot after each scan, so new, modified, and removed files are reflected on the next run. Patterns match each file's name (not its relative directory); `*` matches any sequence and `?` matches one character. Separate patterns with semicolons. `interval_seconds` is optional and defaults to 60 (valid range: 1 through 86,400).

The service defaults to the LocalSystem account. For remote UNC shares, grant the service account access or configure a dedicated account for the service with Windows Service Manager before starting it. Drive-letter mappings from an interactive user session are not available to services; use UNC paths.

## Service commands

Run an elevated terminal in the executable directory. The commands are passed as the final command-line argument, following the lab's SCP model:

```powershell
.\SMIT52.exe install
.\SMIT52.exe start
.\SMIT52.exe status
.\SMIT52.exe stop
.\SMIT52.exe remove
```

`remove` asks SCM to stop the service and marks it for deletion. With no command, SCM launches the service entry point. Errors from service execution are written to the Windows Application event log under `SMIT52BackupService` and to the debugger output stream.

## Notes

- The service reports `START_PENDING`, `RUNNING`, `STOP_PENDING`, and `STOPPED` to SCM and handles stop, shutdown, and interrogate controls.
- A new archive is written to a temporary sibling file and replaces the previous archive only after the ZIP is complete.
- Symbolic links are skipped to avoid traversing outside the configured source tree.
