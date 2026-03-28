#!/bin/bash
python3 -m http.server 8083 &
sleep 1
xdg-open http://localhost:8083/
