// anon_mrc_probe — EXPERIMENT (2026-09-09). Does an ANONYMOUS Steam session get
// a manifest request code for content it does not own?
//
// The providers (wudrm/opensteamtool) served MRCs for the whole catalogue
// without owning the games. Their session therefore was not an ordinary owning
// account. Anonymous login is the obvious candidate: dedicated-server / tooling
// sessions log in anonymously and historically had broader MRC access. We never
// tested it — every measurement so far used the user's own logged-in (spoofed)
// session, which is denied for unowned paid content.
//
// This logs in ANONYMOUSLY and asks Valve for the code for:
//   - a FREE app (control, expected: granted)
//   - a PAID unowned depot (the test)
// then, for any code it gets, tests it against the CDN.
//
// Run:  node probe.js
// Needs: npm i steam-user  (in this dir)
const SteamUser = require('steam-user');
const https = require('https');

// (appid, depotid, manifestid, label)
const CASES = [
  [570,     373301,  '9221106328626754478', 'Dota2 FREE (control)'],
  [1942280, 1942282, '4872816150142449642', 'Brotato PAID content (test)'],
  [2545360, 2545361, '9107064576136045598', 'Lonely Mountains PAID new ver'],
];
const CDN_HOST = 'cache1-par1.steamcontent.com';

function cdnCheck(depot, manifest, code) {
  return new Promise((resolve) => {
    const path = `/depot/${depot}/manifest/${manifest}/5/${code}`;
    https.get({host: CDN_HOST, path, timeout: 10000}, (res) => {
      res.resume();
      resolve(res.statusCode);
    }).on('error', () => resolve('ERR')).on('timeout', function(){this.destroy();resolve('TIMEOUT');});
  });
}

const user = new SteamUser();
user.logOn({anonymous: true});

user.on('loggedOn', async () => {
  console.log('== logged on ANONYMOUSLY, steamID=' + user.steamID + ' ==');
  for (const [app, depot, manifest, label] of CASES) {
    try {
      let code = await user.getManifestRequestCode(app, depot, manifest);
      // steam-user may return a BigInt/Long/object; normalise to a decimal string.
      if (code && typeof code === 'object')
        code = (code.manifest_request_code !== undefined) ? code.manifest_request_code : code.toString();
      code = String(code);
      console.log(`\n[${label}] app=${app} depot=${depot}`);
      console.log(`  getManifestRequestCode -> ${code}`);
      if (code && code !== '0' && /^[0-9]+$/.test(code)) {
        const http = await cdnCheck(depot, manifest, code);
        console.log(`  CDN /depot/${depot}/manifest/${manifest}/5/${code} -> HTTP ${http}`+
                    (http === 200 ? '   <<<<< DOWNLOADS' : ''));
      } else {
        console.log('  (no code granted)');
      }
    } catch (e) {
      console.log(`\n[${label}] app=${app} depot=${depot} -> DENIED/err: ${e.message || e}`);
    }
  }
  console.log('\n== done ==');
  user.logOff();
  process.exit(0);
});

user.on('error', (e) => { console.log('LOGON ERROR: ' + (e.message||e)); process.exit(1); });
setTimeout(() => { console.log('TIMEOUT: no CM connection in 40s (proxy blocking CM?)'); process.exit(2); }, 40000);
