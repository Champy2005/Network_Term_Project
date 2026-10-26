/** Capture every mechanism for human visual review, checking actual browser errors and scene budgets. */
import { test, expect } from '@playwright/test';
import { CHAPTERS } from '../../src/content';
test('every chapter presents its mechanism without runtime failures', async ({ page }, info) => {
  test.setTimeout(120_000);
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/');
  await expect(page.locator('#loading')).toBeHidden();
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  const metrics = [];
  for (let i = 0; i < CHAPTERS.length; i++) {
    await page.locator(`.chapter-segments [data-chapter="${i}"]`).click();
    const c = CHAPTERS[i],
      beat = c.beats.find((b) => b.view === 'detail' || b.view === 'cutaway') ?? c.beats[2];
    await page
      .getByRole('slider', { name: 'Chapter position' })
      .fill(String(Math.min(beat.end - 0.1, beat.start + 3)));
    await expect(page.locator('#app')).toHaveAttribute('data-beat', beat.id);
    await expect
      .poll(
        async () =>
          JSON.parse((await page.locator('#render-metrics').textContent()) || '{}').beatId,
      )
      .toBe(beat.id);
    const data = JSON.parse((await page.locator('#render-metrics').textContent())!);
    metrics.push(data);
    expect(data.triangles).toBeLessThan(500_000);
    expect(data.drawCalls).toBeLessThan(300);
    await page.screenshot({
      path: info.outputPath(`chapter-${String(i + 1).padStart(2, '0')}-${c.id}.png`),
    });
  }
  expect(errors).toEqual([]);
  await info.attach('all-chapter-diagnostics.json', {
    body: JSON.stringify(metrics, null, 2),
    contentType: 'application/json',
  });
});
