let chain = Promise.resolve();
for (let index = 0; index < 600; ++index) chain = chain.then(() => {});
await chain;
