/** Observable browser paths, including negative delivery and frozen scene evidence. */
import { test, expect, type Page } from '@playwright/test';
import { createHash } from 'node:crypto';
const hash = (b: Buffer) => createHash('sha256').update(b).digest('hex');

async function ready(page: Page, query = '') {
  await page.goto('/' + query);
  await expect(page.locator('#loading')).toBeHidden();
  await page.evaluate(() => document.fonts.ready);
}
async function chapter(page: Page, index: number) {
  await page.getByRole('button', { name: 'Chapters', exact: true }).click();
  await page.locator(`.chapter-card[data-chapter="${index}"]`).click();
  await expect(page.locator('#overlay')).not.toBeVisible();
}
test('opens with a clear journey and keeps all initial requests local', async ({ page }, info) => {
  const remote: string[] = [];
  page.on('request', (r) => {
    if (!r.url().startsWith('http://127.0.0.1:4173') && !r.url().startsWith('data:'))
      remote.push(r.url());
  });
  await ready(page);
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('InsideSocket Chat.');
  await expect(page.getByText('15:45 + EXPLORATION')).toBeVisible();
  expect(remote).toEqual([]);
  await page.screenshot({ path: info.outputPath('opening-1366.png') });
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'playing');
  await expect(page.locator('#app')).toHaveAttribute('data-chapter', 'system-map');
});
test('pause freezes both narrative time and actual rendered pixels', async ({ page }) => {
  await ready(page);
  await chapter(page, 3);
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'playing');
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  const time = await page.locator('#app').getAttribute('data-time');
  const image = await page.locator('#world-canvas').screenshot();
  await page.waitForTimeout(350);
  expect(await page.locator('#app').getAttribute('data-time')).toBe(time);
  expect(hash(await page.locator('#world-canvas').screenshot())).toBe(hash(image));
});
test('inspection steps a split read, resets it, and restores the journey', async ({
  page,
}, info) => {
  await ready(page);
  await chapter(page, 3);
  await page.getByRole('button', { name: 'Stop & inspect', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-mode', 'inspection');
  await page.locator('[data-experiment="split-header"]').click();
  await expect(page.locator('#semantic-state')).toContainText('"emitted":0');
  await expect(page.locator('.experiment-metrics')).toContainText('2 / 4');
  await page.locator('#experiment-next').click();
  await expect(page.locator('#semantic-state')).toContainText('"emitted":1');
  await page.screenshot({ path: info.outputPath('split-header-complete.png') });
  await page.getByRole('button', { name: 'Reset', exact: true }).click();
  await expect(page.locator('#semantic-state')).toContainText('"emitted":0');
  await page.getByRole('button', { name: 'Leave experiment', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-time', '0.000');
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'paused');
});
test('forged metadata produces no deliveries, with a visible denial', async ({ page }) => {
  await ready(page);
  await chapter(page, 7);
  await page.locator('#inspect').click();
  await page.locator('[data-experiment="forged-sender"]').click();
  await page.locator('#experiment-next').click();
  await expect(page.locator('#semantic-state')).toContainText('"deliveries":[]');
  await expect(page.locator('.result-status')).toHaveAttribute('data-status', 'rejected');
});
test('source atlas opens current evidence and loads a full file lazily', async ({ page }) => {
  await ready(page);
  await chapter(page, 7);
  await page.getByRole('button', { name: 'Source atlas', exact: true }).click();
  await expect(page.locator('#source-detail')).toContainText('src/server/chat_hub.cpp');
  await expect(page.locator('#source-detail code')).toContainText('ChatHub::direct');
  await page.locator('[data-file="src/protocol/framing.cpp"]').click();
  await expect(page.locator('#source-detail code')).toContainText('FrameDecoder::finish');
  await page.getByRole('button', { name: 'Close overlay', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'paused');
});
test('a corrupted lazy source is refused', async ({ page }) => {
  await page.route('**/content/source-files/src__protocol__framing.cpp.txt', (route) =>
    route.fulfill({ status: 200, body: 'altered source' }),
  );
  await ready(page);
  await page.getByRole('button', { name: 'Source atlas', exact: true }).click();
  await page.locator('[data-file="src/protocol/framing.cpp"]').click();
  await expect(page.locator('#source-detail')).toContainText('failed its integrity check');
});
test('supports the complete illustrated experience without WebGL', async ({ page }) => {
  await ready(page, '?graphics=static');
  await expect(page.locator('#poster')).toBeVisible();
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await chapter(page, 10);
  await expect(page.locator('#accessible-schematic')).toContainText('30 s');
  await page.getByRole('button', { name: 'Transcript', exact: true }).click();
  await expect(page.locator('.transcript-entry')).toHaveCount(71);
});
test('reduced motion steps transforms while the narrative continues', async ({ page }) => {
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await ready(page);
  await chapter(page, 3);
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  const before = await page
    .locator('#world-canvas')
    .screenshot({ mask: [page.locator('#journey-footer')] });
  await page.waitForTimeout(350);
  const after = await page
    .locator('#world-canvas')
    .screenshot({ mask: [page.locator('#journey-footer')] });
  expect(hash(after)).toBe(hash(before));
  expect(Number(await page.locator('#app').getAttribute('data-time'))).toBeGreaterThan(0);
});
test('settings persist and corrupt browser storage does not break startup', async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem('inside-socket-chat/v1', '{bad'));
  await ready(page);
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await page.getByRole('button', { name: 'Settings', exact: true }).click();
  await page.locator('#reduced-motion').check();
  await page.getByRole('button', { name: 'Close overlay', exact: true }).click();
  const data = await page.evaluate(() =>
    JSON.parse(localStorage.getItem('inside-socket-chat/v1')!),
  );
  expect(data.preferences.reducedMotion).toBe(true);
  expect(data.chapter).toBe(0);
});
test('mobile layout retains accessible navigation and playback', async ({ page }, info) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await ready(page);
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Pause', exact: true })).toBeInViewport();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await page.screenshot({ path: info.outputPath('mobile-390.png') });
});
test('narration recovers when its manifest arrives after playback begins', async ({ page }) => {
  let release: () => void = () => {};
  const gate = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route('**/assets/audio-manifest.json', async (route) => {
    const response = await route.fetch();
    await gate;
    await route.fulfill({ response });
  });
  await ready(page, '?graphics=static');
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'playing');
  release();
  await expect
    .poll(
      async () =>
        JSON.parse((await page.locator('#audio-metrics').textContent()) || '{}').source || '',
    )
    .toContain('system-map');
  await expect
    .poll(
      async () =>
        JSON.parse((await page.locator('#audio-metrics').textContent()) || '{}').readyState ?? 0,
    )
    .toBeGreaterThanOrEqual(2);
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await expect
    .poll(
      async () => JSON.parse((await page.locator('#audio-metrics').textContent()) || '{}').paused,
    )
    .toBe(true);
});
for (const manifest of [{}, { cues: null }]) {
  test(`malformed narration ${JSON.stringify(manifest)} leaves playback available`, async ({
    page,
  }) => {
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    await page.route('**/assets/audio-manifest.json', (route) => route.fulfill({ json: manifest }));
    await ready(page, '?graphics=static');
    await expect(page.locator('#notice')).toContainText('Narration assets are unavailable');
    await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
    await expect
      .poll(async () => Number(await page.locator('#app').getAttribute('data-time')))
      .toBeGreaterThan(0.2);
    await page.getByRole('button', { name: 'Pause', exact: true }).click();
    await page.getByRole('button', { name: 'Next beat', exact: true }).click();
    await expect(page.locator('#app')).toHaveAttribute('data-time', '9.000');
    await expect(page.locator('#caption')).toBeVisible();
    expect(errors).toEqual([]);
  });
}

test('chapter holds and explicit Continue remain accessible', async ({ page }) => {
  await ready(page, '?graphics=static');
  await chapter(page, 0);
  await page.getByRole('slider', { name: 'Chapter position' }).fill('45');
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'held');
  await page.getByRole('button', { name: 'Continue', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-chapter', 'architecture');
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'playing');
});
test('unavailable model assets retain a working functional presentation', async ({ page }) => {
  await page.route('**/assets/models/*.glb', (route) =>
    route.fulfill({ status: 404, body: 'missing test asset' }),
  );
  await ready(page);
  await expect(page.locator('#notice')).toContainText('functional cutaway remains available');
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'playing');
});
test('a real graphics context loss pauses into the illustrated journey', async ({ page }) => {
  await ready(page);
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await page.evaluate(() => {
    const canvas = document.querySelector<HTMLCanvasElement>('#world-canvas')!;
    const extension = canvas.getContext('webgl2')!.getExtension('WEBGL_lose_context');
    if (!extension) throw new Error('The browser must expose context-loss testing.');
    extension.loseContext();
  });
  await expect(page.locator('#poster')).toBeVisible();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'paused');
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'playing');
});
test('rejected audio playback exposes a recoverable caption alternative', async ({ page }) => {
  await page.addInitScript(() => {
    HTMLMediaElement.prototype.play = () =>
      Promise.reject(new DOMException('Blocked for this test', 'NotAllowedError'));
  });
  await ready(page, '?graphics=static');
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await expect(page.locator('#notice')).toContainText('Captions are available');
  await expect(page.locator('#caption')).toBeVisible();
});
test('Home and Continue restore the latest in-memory progress', async ({ page }) => {
  await page.addInitScript(() =>
    localStorage.setItem(
      'inside-socket-chat/v1',
      JSON.stringify({ version: 1, chapter: 3, time: 18, preferences: {} }),
    ),
  );
  await ready(page, '?graphics=static');
  await page.getByRole('button', { name: 'Continue where you left off', exact: true }).click();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await page.getByRole('button', { name: 'Next beat', exact: true }).click();
  await expect(page.locator('#app')).toHaveAttribute('data-time', '36.000');
  await page.getByRole('button', { name: 'Stop & inspect', exact: true }).click();
  await page.locator('[data-experiment="split-header"]').click();
  await expect(page.locator('.experiment-panel')).toBeVisible();
  await page.getByRole('button', { name: 'Inside Socket Chat home', exact: true }).click();
  await expect(page.locator('.experiment-panel')).toBeHidden();
  await page.getByRole('button', { name: 'Continue where you left off', exact: true }).click();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  const t = Number(await page.locator('#app').getAttribute('data-time'));
  expect(t).toBeGreaterThanOrEqual(36);
  expect(t).toBeLessThan(37);
});
test('scrubbing suspends narration and playback until the learner resumes', async ({ page }) => {
  await ready(page, '?graphics=static');
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await page.getByRole('slider', { name: 'Chapter position' }).fill('20');
  await expect(page.locator('#app')).toHaveAttribute('data-phase', 'paused');
  await expect(page.locator('#app')).toHaveAttribute('data-time', '20.000');
  await expect
    .poll(
      async () => JSON.parse((await page.locator('#audio-metrics').textContent()) || '{}').paused,
    )
    .toBe(true);
});
