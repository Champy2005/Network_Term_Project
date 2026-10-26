/** Reviewed reference frames for the Windows/Chromium software-rendering test configuration. */
import { test, expect } from '@playwright/test';
for (const size of [
  { width: 1366, height: 768 },
  { width: 1920, height: 1080 },
]) {
  test(`opening composition ${size.width}`, async ({ page }) => {
    test.setTimeout(60_000);
    await page.setViewportSize(size);
    await page.goto('/');
    await expect(page.locator('#loading')).toBeHidden();
    await page.evaluate(() => document.fonts.ready);
    await expect(page).toHaveScreenshot(`opening-${size.width}.png`, {
      animations: 'disabled',
      maxDiffPixelRatio: 0.002,
      timeout: 20000,
    });
  });
}
test('framing cutaway at a deterministic beat', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('#loading')).toBeHidden();
  await page.getByRole('button', { name: 'Chapters', exact: true }).click();
  await page.locator('.chapter-card[data-chapter="3"]').click();
  await page.getByRole('button', { name: 'Next beat', exact: true }).click();
  await expect(page).toHaveScreenshot('framing-cutaway.png', {
    animations: 'disabled',
    maxDiffPixelRatio: 0.002,
  });
});
