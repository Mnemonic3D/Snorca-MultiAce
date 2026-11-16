"""Instrument only an expendable CI app copy, never a release artifact."""
import http.server
import json
import pathlib
import sys
import urllib.parse

app, evidence = map(pathlib.Path, sys.argv[1:])
page = app / "Contents/Resources/web/flutter_web/index.html"
script = """<script>
(() => {
  const report = (kind, value) => fetch('http://127.0.0.1:39751/?event=' +
    encodeURIComponent(JSON.stringify({kind, value})), {mode:'no-cors'}).catch(() => {});
  window.addEventListener('error', e => report('error', {message:e.message, file:e.filename, line:e.lineno, stack:e.error?.stack}));
  window.addEventListener('unhandledrejection', e => report('rejection', String(e.reason)));
  for (const name of ['error','warn']) {
    const original = console[name];
    console[name] = (...args) => {report(name, args.map(String)); original.apply(console,args);};
  }
  report('begin', navigator.userAgent);
  setTimeout(() => {
    const gl = document.createElement('canvas').getContext('webgl2');
    report('webgl', gl ? {version:gl.getParameter(gl.VERSION),renderer:gl.getParameter(gl.RENDERER)} : null);
    report('document', {url:location.href, html:document.body.innerHTML.slice(0,20000)});
  }, 45000);
})();
</script>"""
page.write_text(page.read_text().replace("<head>", "<head>" + script, 1))
(evidence / "instrumentation.txt").write_text("Diagnostic copy only: HTML error hooks inserted and app ad-hoc signed again. Original DMG unchanged.\n")

class Recorder(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        value = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("event", [""])[0]
        with (evidence / "web-events.jsonl").open("a") as output:
            output.write(json.dumps({"event": value}) + "\n")
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

    def log_message(self, *args):
        pass

http.server.HTTPServer(("127.0.0.1", 39751), Recorder).serve_forever()
