/**
 * @file main.cpp
 * @author linome (francukigor549@gmail.com)
 * @brief Main entry point of backup service for Windows.
 * @version 0.1.0
 * @date 2026-10-24
 * 
 * @details
 * Program should work as a service on Windows (7 and later).
 * Program should perform backup of files from specified directory to archive in ZIP format.
 *
 * General requirements:
 * 1. The program must not be interactive, all settings must be provided via configuration files.
 * 2. The program must create archive with backup of files from specified directory.
 * 3. Files to be backed up are specified with a mask (supports * and ? wildcards) or by names.
 * 4. Backup archive must use the ZIP format.
 * 5. Archivation must not utilize any external software.
 * 6. New files that fit a mask and changes to existing files must be added to the archive.
 * 7. Installation, uninstallation, start, and stop of the service must be performed via command line.
 * 8. Correct start/stop of the service when requested by SCM
 * 9. File/directory names are specified in UNC format (\\localhost\C$\path\to\file.txt)
 *
 * Configuration file format requirements:
 * 1. Name of directory to be backed up.
 * 2. Name of archive file to be created.
 * 3. Mask/names of files to be backed up.
 */

#include <iostream>

#include <Windows.h>

#include "main.hpp"

int main(int argc, char** argv) {
    std::cout << "Hello, from SMIT52!\n";

    return 0;
}
