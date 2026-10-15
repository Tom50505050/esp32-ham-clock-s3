#!/bin/bash
# Uruchom/restart serwera plikow na portach 12000 (work-1) i 12001 (work-2).
# Serwer pada przy resecie srodowiska - po prostu uruchom ten skrypt ponownie.
# UWAGA: `setsid` jest konieczne - zwykle `nohup ... &` jest ubijane po
# zakonczeniu komendy w tym srodowisku (proces nie odczepia sie od sesji).
cd /workspace/share || exit 1
pkill -f "http.server 12000" 2>/dev/null
pkill -f "http.server 12001" 2>/dev/null
sleep 1
setsid python3 -m http.server 12000 --bind 0.0.0.0 > /workspace/share/httpd.log 2>&1 < /dev/null &
setsid python3 -m http.server 12001 --bind 0.0.0.0 > /workspace/share/httpd12001.log 2>&1 < /dev/null &
sleep 3
echo "Serwery na portach 12000 i 12001. Test:"
curl -s -o /dev/null -w "http://127.0.0.1:12000/ -> HTTP %{http_code}\n" http://127.0.0.1:12000/
curl -s -o /dev/null -w "http://127.0.0.1:12001/ -> HTTP %{http_code}\n" http://127.0.0.1:12001/
