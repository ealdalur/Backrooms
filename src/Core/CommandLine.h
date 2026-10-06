#pragma once
// ---------------------------------------------------------------------------
// CommandLine.h
// Reads the command line into EngineOptions, and prints its help:
//
//   Backrooms --help            every option (spoiler-free)
//   Backrooms --help demo       every developer scene, and what --type does in each
//   Backrooms --help <scene>    one scene, e.g. --help phone
//   Backrooms --help puzzle     the stages --puzzle can start at
//
// The help also answers to the forms people try first: a bare --demo,
// --demo --help, --demo help, --demo <scene> --help, --puzzle --help.
// The scenes and stages are listed here, in CommandLine.cpp, so an unknown
// name, or an option missing its value, is caught before anything starts.
// ---------------------------------------------------------------------------

struct EngineOptions;

/// Fills `options` from the command line. False if the program should exit
/// instead of running - the help was printed (`exitCode` 0) or the command
/// line was wrong (the error printed, `exitCode` 1).
bool parseCommandLine(int argc, char* argv[], EngineOptions& options, int& exitCode);
