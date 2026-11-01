# Loads the web page in headless Chrome against machost --testrom and checks the live screen + sound.
import asyncio, sys
from playwright.async_api import async_playwright
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch(executable_path='/usr/bin/google-chrome', args=['--autoplay-policy=no-user-gesture-required'])
        pg = await b.new_page(viewport={'width': 1200, 'height': 1000})
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        await pg.goto('http://localhost:8080/')
        await pg.wait_for_timeout(2500)
        ok = await pg.evaluate('''() => { const c=document.getElementById('scr').getContext('2d');
            const d=c.getImageData(0,0,512,342).data; let bad=0;
            for(let y=0;y<342;y++)for(let x=0;x<512;x++){const byte=(y*64+(x>>3))&255;const bit=(byte>>(7-(x&7)))&1;
              const px=d[(y*512+x)*4]; if((px==0)!=(bit==1))bad++} return bad }''')
        print('canvas bad pixels:', ok)
        await pg.click('#bSound')
        await pg.wait_for_timeout(1500)
        print('sound button:', await pg.inner_text('#bSound'))
        print('status:', (await pg.inner_text('#st')).replace('\n', ' | '))
        await pg.screenshot(path='page.png', full_page=True)
        print('page errors:', errs)
        await b.close()
asyncio.run(main())
