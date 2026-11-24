export function isDemo(search: string): boolean {
  return new URLSearchParams(search).get('demo') === '1'
}
