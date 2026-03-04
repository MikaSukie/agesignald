#!/bin/bash
clear
  python build.py
time bash -c '
  ./agesignald
  echo "Exit code: $?"
'
