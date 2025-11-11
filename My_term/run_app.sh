#!/bin/bash

# This line is important! It changes the current directory
# to the script's directory, ensuring 'make' finds the Makefile.
cd "$(dirname "$0")"

# Use the 'run' target you already defined in your Makefile.
# This will build the project and then execute it.
make run