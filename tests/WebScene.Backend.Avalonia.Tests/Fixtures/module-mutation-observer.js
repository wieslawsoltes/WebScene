window.__moduleMutationObserverCalls = 0;
new MutationObserver(function () {
  window.__moduleMutationObserverCalls++;
}).observe(document.body, { childList: true, subtree: true });
