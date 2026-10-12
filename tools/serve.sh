#!/bin/bash
# Uruchom/restart serwera plikow na porcie 12000 (work-1).
# Serwer pada przy resecie srodowiska - po prostu uruchom ten skrypt ponownie.
cd /workspace/share || exit 1
pkill -f "http.server 12000" 2>/dev/null
sleep 1
nohup python3 -m http.server 12000 --bind 0.0.0.0 > /workspace/share/httpd.log 2>&1 &
disown
sleep 2
echo "Serwer na porcie 12000. Test:"
curl -s -o /dev/null -w "http://127.0.0.1:12000/ -> HTTP %{http_code}\n" http://127.0.0.1:12000/
