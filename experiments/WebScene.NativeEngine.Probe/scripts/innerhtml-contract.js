(function () {
  const host = document.createElement("div");
  document.body.appendChild(host);
  host.innerHTML = '<iframe id="innerhtml-contract-frame" src="about:blank"></iframe>';
  if (!host.querySelector("#innerhtml-contract-frame")) {
    throw new Error("native innerHTML iframe parsing contract failed");
  }
})();
