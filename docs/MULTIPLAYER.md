# Multiplayer (System Link)

**Setup 1.2 and later include System Link.** Each player needs the same build and their own
game files. Use **Multiplayer > System Link** in the game.

Configure the connection before starting the game:

1. In the launcher's **Settings**, tick **Enable System Link**.
2. Set **Local UDP port** to `3075` on each PC.
3. In **Peers (host:port, comma separated)**, enter the other PC's reachable IPv4
   address and port, for example `192.168.1.51:3075`. Configure both PCs: each needs
   the other's address. Use the PC's LAN, VPN or public address as appropriate,
   rather than the emulated guest address printed in the game log.
4. Allow the game's chosen UDP port through Windows Firewall on the network you
   are using. See Microsoft's [Windows Firewall rule documentation](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule).

For **two PCs on the same LAN**, find each PC's IPv4 address in Windows network
settings or with `ipconfig`. Both PCs must be able to reach each other; guest Wi-Fi
or wireless client isolation can prevent this. You can also launch from PowerShell 7
in the repository folder, replacing these example addresses:

```powershell
# PC A: its LAN address is 192.168.1.50; PC B is 192.168.1.51.
pwsh scripts\play.ps1 -Instance Online -Network -NetworkPort 3075 -NetPeers '192.168.1.51:3075'

# PC B: point back to PC A.
pwsh scripts\play.ps1 -Instance Online -Network -NetworkPort 3075 -NetPeers '192.168.1.50:3075'
```

Separate PCs can both use port `3075`. Multiple copies on **one PC** need different
ports and different `-Instance` names; see the [two-instance example](../DEVELOPERS.md#pc-runtime-upgrades).
Named instances start with separate profiles and save preferences under
`%LOCALAPPDATA%\MM3Recomp\instances\<name>`, while sharing the installed game data.
The command-line network options override saved launcher settings.

For **internet play**, choose one of these connection methods, then follow the
same in-game steps below:

- **Private VPN:** connect all players to a private VPN that allows UDP traffic
  between their PCs. Enter each other's VPN IPv4 address and game port in **Peers**,
  and allow that traffic through the VPN's access rules and Windows Firewall.
- **Direct UDP:** on each player's router, forward a chosen external **UDP** port
  to that PC's LAN IPv4 address and game UDP port. Exchange public IPv4 addresses
  and external ports, and use those endpoints in **Peers**. A private LAN address
  such as `192.168.1.50` is not reachable across the internet. If an ISP uses CGNAT,
  or another router prevents inbound forwarding, use a private VPN with reachable
  peer addresses instead.

Every player needs mutual UDP reachability. This backend provides no public lobby
service, relay, automatic port forwarding or NAT hole punching. Its UDP tunnel
has no authentication or encryption; connect to trusted peers or use a private VPN.

For **three to eight players**, configure each PC's comma-separated **Peers** list
with **every other player**. For example, A lists B and C, B lists A and C, and C
lists A and B. The host does not relay the other players' tunnel traffic. Peer
entries accept IPv4 addresses or hostnames that resolve to IPv4, followed by a
port; up to eight peer entries can be stored. Use commas without spaces, for
example `192.168.1.51:3075,192.168.1.52:3075`.

To create and join a game:

1. Choose a profile, then **Multiplayer > System Link**.
2. **Host:** press **Y — Create New Game**, select **Done** and press **A** to accept
   the name, then choose the session settings and vehicle. Start with **Cruise**
   and a base-game car when testing the connection.
3. **Other players:** select the host's session and press **A** to join.
4. **Clients:** press **X — Ready** in the lobby.
5. **Host:** once the players are ready, press **X — Start Game**.

These are Xbox button prompts. With the default keyboard bindings, **Y = S**,
**A = Z**, and **X = A**; bindings changed in the launcher's **Controls** take
precedence. Xbox Live sign-in and Insignia service support have not been validated.

If the host does not appear, check that System Link is enabled on every PC,
the peer addresses and ports are reciprocal, and firewall/VPN rules allow the
chosen UDP port. `127.0.0.1` works only for copies on the same PC. If the port is
already in use, choose a different local port and update the other players' peer
lists. If joining fails, first retry with the same build, game content and base-game
cars on all PCs. Launch logs are under `logs\`; include them when reporting a problem.

**Tested:** two native copies on one Windows PC discovered a session, joined,
propagated Ready, started a Paris Cruise and exchanged moving vehicle state in both
directions. Separate physical PCs, VPNs and public-internet latency/loss conditions
have not yet been tested.
