#!/bin/bash
python3 -m http.server 8081 &
sleep 1
xdg-open http://localhost:8081/
