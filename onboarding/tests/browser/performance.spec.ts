/** Reproducible software-rendered diagnostics, not a claim about physical laptop performance. */
import { test, expect } from '@playwright/test';
import { writeFile } from 'node:fs/promises';
import { CHAPTERS } from '../../src/content';
test('records rendering and resource counts across chapter traversal', async ({ page }, info) => {
  test.setTimeout(60000);
  await page.goto('/');
  await expect(page.locator('#loading')).toBeHidden();
  await page.getByRole('button', { name: 'Begin the journey', exact: true }).click();
  await expect
    .poll(
      async () =>
        JSON.parse((await page.locator('#render-metrics').textContent()) || '{}').samples ?? 0,
      { timeout: 30000 },
    )
    .toBeGreaterThan(0);
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  const samples = [];
  for (const index of [0, 3, 5, 7, 9, 11, 0]) {
    await page.locator(`.chapter-segments [data-chapter="${index}"]`).click();
    await expect
      .poll(
        async () =>
          JSON.parse((await page.locator('#render-metrics').textContent()) || '{}').chapterId,
      )
      .toBe(CHAPTERS[index].id);
    await expect
      .poll(
        async () =>
          JSON.parse((await page.locator('#render-metrics').textContent()) || '{}').samples ?? 0,
      )
      .toBeGreaterThan(0);
    await page.waitForTimeout(250);
    samples.push({
      chapter: index,
      ...JSON.parse((await page.locator('#render-metrics').textContent()) || '{}'),
    });
  }
  expect(samples.every((s) => s.triangles < 500_000)).toBe(true);
  expect(samples.every((s) => s.drawCalls < 300)).toBe(true);
  const reportPath = info.outputPath('render-diagnostics.json');
  await writeFile(
    reportPath,
    JSON.stringify(
      { renderer: 'Playwright Chromium / SwiftShader', viewport: '1366x768', samples },
      null,
      2,
    ),
  );
  await info.attach('render-diagnostics.json', {
    path: reportPath,
    contentType: 'application/json',
  });
});
