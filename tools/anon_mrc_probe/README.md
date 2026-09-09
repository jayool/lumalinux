# anon_mrc_probe

Experiment: does an anonymous Steam session get manifest request codes for
unowned paid content? Tests the hypothesis that the code providers used
anonymous logins (they served the whole catalogue without owning games).

    cd tools/anon_mrc_probe
    npm init -y >/dev/null 2>&1
    npm i steam-user
    node probe.js

Reads nothing local; logs in anonymously to Steam's CM and asks for codes for a
free control app and paid unowned depots, then tests any code against the CDN.
