#!/bin/sh
# pump: removes every pump plugin from this Mac (double-click it; it asks for your password once).
cd "$(dirname "$0")" || exit 1
echo ""
echo "...pump uninstall"
echo "================="
echo "This removes every pump plugin (VST3, Audio Unit, CLAP) from /Library/Audio/Plug-Ins and your own"
echo "~/Library/Audio/Plug-Ins. System EQ, if installed, stays (drag it from Applications to the Trash)."
echo ""
if sudo sh ./installer/pump-cleanup.sh --uninstall; then
  for p in vst3 au clap cleanup refresh; do sudo pkgutil --forget "com.wake.pump.$p" >/dev/null 2>&1; done
  echo "Done. In your host, rescan plug-ins."
else
  echo "Nothing was changed."
fi
echo ""
printf "Press Enter to close "
read -r _
