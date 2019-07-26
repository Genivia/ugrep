PHP patterns
============

- `comments` matches comments, auto-enables ugrep option -o
- `functions` matches function definitions
- `names` matches identifiers (and keywords)
- `strings` matches strings, auto-enables ugrep option -o to match multi-line strings
- `zap_comments` removes comments from matches, recommend ugrep option -o
- `zap_html` removes HTML content outside of `<?php...?>` blocks, auto-enables ugrep option -o
- `zap_strings` removes strings from matches, recommend ugrep option -o
