window.__webSceneReactChunkEvaluationCount =
  Number(window.__webSceneReactChunkEvaluationCount || 0) + 1;
window.__webSceneReactChunkOrder = ['evaluation'];
Promise.resolve().then(function () { window.__webSceneReactChunkOrder.push('microtask'); });
