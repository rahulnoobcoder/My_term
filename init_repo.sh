#!/bin/bash

# This script automates the initial git setup and first push

echo "--- 1. Staging all files..."
git add .

echo "--- 2. Committing with message 'first commit'..."
git commit -m "first commit"

echo "--- 3. Renaming branch to 'main'..."
git branch -M main

echo "--- 4. Adding remote 'origin'..."
git remote add origin https://github.com/rahulnoobcoder/My_term.git

echo "--- 5. Pushing to origin main..."
git push -u origin main

echo "--- All done! ---"