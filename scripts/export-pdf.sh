#!/usr/bin/env bash
# Export the slide deck to a PDF, one slide per page.
#
# The PDF is the fallback for the day the projector, the laptop or the browser
# misbehaves. It is deliberately not committed: regenerate it when the deck
# changes.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deck="$here/slides/index.html"
out="${1:-$here/zephyr-workshop-2026-slides.pdf}"

chrome=""
for candidate in google-chrome-stable google-chrome chromium chromium-browser; do
	if command -v "$candidate" >/dev/null 2>&1; then
		chrome="$candidate"
		break
	fi
done

if [ -z "$chrome" ]; then
	echo "error: no Chrome or Chromium binary found" >&2
	exit 1
fi

[ -f "$deck" ] || { echo "error: deck not found at $deck" >&2; exit 1; }

"$chrome" \
	--headless \
	--disable-gpu \
	--no-pdf-header-footer \
	--virtual-time-budget=10000 \
	--print-to-pdf="$out" \
	"file://$deck" >/dev/null 2>&1

[ -s "$out" ] || { echo "error: chrome produced no output" >&2; exit 1; }

echo "PDF: $out"
