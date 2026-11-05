// FellowEngine's web check (milestone 50): opens the page in headless Chromium (WebGL 2 on SwiftShader), waits for
// the project's WEB_CHECK line, and writes web_check.json (the line, the console's errors and warnings) and
// web_check.png: node web_check.js URL OUT_DIR [timeout_s]
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

(async () => {
	const [url, out, timeoutS] = [process.argv[2], process.argv[3], parseFloat(process.argv[4] || '600')];
	const browser = await chromium.launch({ args: ['--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'] });
	const page = await browser.newPage({ viewport: { width: 960, height: 540 } });
	const consoleLines = [];
	let check = null;
	let started = null;
	const t0 = Date.now();
	page.on('console', (m) => {
		const text = m.text();
		consoleLines.push(`[${m.type()}] ${text}`);
		if (text.startsWith('WEB_CHECK ')) {
			check = JSON.parse(text.substring(10));
		}
		if (text === 'WEB_STARTED') {
			started = (Date.now() - t0) / 1000;
		}
	});
	page.on('pageerror', (e) => consoleLines.push(`[pageerror] ${e}`));
	await page.goto(url);
	const isolated = await page.evaluate(() => self.crossOriginIsolated);
	while (!check && Date.now() - t0 < timeoutS * 1000) {
		await page.waitForTimeout(1000);
	}
	await page.screenshot({ path: path.join(out, 'web_check.png') });
	const problems = consoleLines.filter((l) => /^\[(error|pageerror)\]|ERROR|WARNING/.test(l));
	const result = { url, cross_origin_isolated: isolated, started_s: started, seconds: (Date.now() - t0) / 1000, check, problems: problems.slice(0, 40), console_lines: consoleLines.length };
	fs.writeFileSync(path.join(out, 'web_check.json'), JSON.stringify(result, null, 1));
	fs.writeFileSync(path.join(out, 'web_console.log'), consoleLines.join('\n'));
	console.log(JSON.stringify(result, null, 1));
	await browser.close();
	process.exit(check ? 0 : 1);
})();
