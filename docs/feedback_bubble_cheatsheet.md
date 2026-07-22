# Feedback Bubble Cheat Sheet

| Mechanism | Shell radius $R_s(t)$ | Asymptotic scaling | Conditions |
|:---|:---|:---|:---|
| **Photoionization** | $R_s = R_{\rm St,0}\left(1 + \dfrac{7c_s t}{4R_{\rm St,0}}\right)^{4/7}$ | $R_s \propto t^{4/7}$ | Radiation pressure negligible compared to gas pressure | 
| **Stellar winds** | $R_s = 0.76\left(\dfrac{\dot{M}v_w^2}{\rho_0}\right)^{1/5} t^{3/5}$ | $R_s \propto t^{3/5}$ | Wind bubble with hot, radiatively-inefficient shocked wind component has been established. |
| **Radiation pressure** | $R_s = \left(\dfrac{3\dot{P}}{2\pi\rho_0}\right)^{1/4} t^{1/2}$ | $R_s \propto t^{1/2}$ | Photons absorbed in a thin optically-thick shell; also works for radiatively-efficient winds with $\dot{P}=\dot{M}v_{\rm w}$. |
| **SNe: free expansion** | $R_s \approx v_{\rm ej}\thinspace  t$ | $R_s \propto t$ | Mass swept out is $<<M_{\rm ej}$ |
| **SNe: Sedov-Taylor** | $R_s = \left(\dfrac{2.026\thinspace  E}{\rho_0}\right)^{1/5} t^{2/5}$ | $R_s \propto t^{2/5}$ | Mass swept out is $>>M_{\rm ej}$ but cooling negligible |
| **SNe: radiative snowplow** | $R_s \approx \left(\dfrac{3p_{\rm cool}}{\pi\rho_0}\right)^{1/4} t^{1/4}$ | $R_s \propto t^{1/4}$ | Gas has cooled ($t>>t_{\rm cool}$) |

---

## Useful expressions for strong ISM shocks

Strong shock conditions ($\mathcal{M} \equiv v_s/V_{\rm ms} \gg 1$), $\gamma = 5/3$ (Draine Ch. 36):

| Quantity | Expression | Notes |
|:---|:---|:---|
| Compression ratio | $\rho_2/\rho_1 \approx 4$ | Eq. 36.22 |
| Post-shock velocity | $u_2 \approx \dfrac{1}{4}v_s$ | Shock frame; Eq. 36.23 |
| Post-shock temperature | $T_2 \approx \dfrac{3}{16}\dfrac{\mu m_H v_s^2}{k}$ | Eq. 36.24 |
| Post-shock temperature | $T_2 \approx 1.38\times10^7\thinspace {\rm K}\left(\dfrac{\mu}{0.609}\right)\left(\dfrac{v_s}{10^3\thinspace {\rm km\thinspace s^{-1}}}\right)^2$ | Fully ionized gas; Eq. 36.28 |
| Cooling time | $t_{\rm cool} \approx 7000\left(\dfrac{{\rm cm}^{-3}}{n_{H,0}}\right)\left(\dfrac{v_s}{100\thinspace {\rm km\thinspace s^{-1}}}\right)^{3.4}\thinspace {\rm yr}$ | $80 \lesssim v_s/{\rm km\thinspace s^{-1}} \lesssim 1200$; Eq. 36.33 |
| | $\approx 7000\left(\dfrac{{\rm cm}^{-3}}{n_{H,0}}\right)\left(\dfrac{0.609\thinspace T_2}{\mu\cdot1.38\times10^5\thinspace {\rm K}}\right)^{1.7}\thinspace {\rm yr}$ | |
| Radiative shock density | $\rho_{\rm ps}/\rho_0 \approx v_s^2/c_{s,0}^2$ | After cooling to preshock $T$ |

---

## Supernova radiative phase quantities

At the transition from Sedov-Taylor to the radiative (snowplow) phase:

| Quantity | Expression |
|:---|:---|
| Transition time | $t_{\rm cool} \approx 40\thinspace{\rm kyr}\thinspace Z^{-1/3}\left(\dfrac{E}{10^{51}\thinspace{\rm erg}}\right)^{0.22}\left(\dfrac{n_{H,0}}{1\thinspace{\rm cm}^{-3}}\right)^{-0.58}$ |
| Transition radius | $R_{\rm cool} \approx 24\thinspace{\rm pc}\thinspace Z^{-2/15}\left(\dfrac{E}{10^{51}\thinspace{\rm erg}}\right)^{0.29}\left(\dfrac{n_{H,0}}{1\thinspace{\rm cm}^{-3}}\right)^{-0.42}$ |
| Terminal momentum | $p_{\rm cool} \approx 3\times10^5\thinspace M_\odot\thinspace{\rm km\thinspace s^{-1}}\thinspace Z^{-1/5}\left(\dfrac{E}{10^{51}\thinspace{\rm erg}}\right)^{0.94}\left(\dfrac{n_{H,0}}{1\thinspace{\rm cm}^{-3}}\right)^{-0.11}$ |

---

## Glossary

| Symbol | Definition |
|:---|:---|
| $\rho_0$ | Ambient ISM mass density |
| $c_s$ | Sound speed in the ionized gas, $c_s = \sqrt{2k_BT/m_H} \approx 13\thinspace {\rm km\thinspace s^{-1}}$ at $T=10^4\thinspace {\rm K}$ |
| $\mathcal{Q}$ | Ionizing photon luminosity (${\rm photons\thinspace s^{-1}}$) |
| $\alpha_B$ | Case B recombination coefficient, $\alpha_B \approx 2.54\times10^{-13} T_4^{-0.833}\thinspace {\rm cm^3\thinspace s^{-1}}$, where $T_4 = T/10^4\thinspace {\rm K}$ |
| $R_{\rm St,0}$ | Initial Strömgren radius, $R_{\rm St,0} = \left(3\mathcal{Q}/4\pi n_H^2\alpha_B\right)^{1/3}$ |
| $\dot{M}$ | Stellar mass-loss rate |
| $v_w$ | Wind terminal velocity |
| $\dot{P}$ | Radiation pressure force, $\dot{P} = Lf_{\rm abs}/c$ |
| $L$ | Stellar (cluster) luminosity |
| $f_{\rm abs}$ | Absorbed fraction of radiation, $f_{\rm abs} = 1 - e^{-\tau}$ |
| $E$ | Supernova explosion energy ($\approx 10^{51}\thinspace {\rm erg}$) |
| $v_{\rm ej}$ | Supernova ejecta velocity |
| $t_{\rm cool}$ | Time at which the Sedov-Taylor remnant becomes radiative |
| $R_{\rm cool}$ | SNR radius at $t_{\rm cool}$ |
| $p_{\rm cool}$ | Terminal momentum at $t_{\rm cool}$, $p_{\rm cool} = \frac{4\pi}{3}\rho_0 R_{\rm cool}^3 \dot{R}_{\rm cool}$ |
| $\mu$ | Mean molecular weight (dimensionless); $\mu = 1.273$ for neutral HI, $\mu = 0.609$ for fully ionized gas (He/H = 0.1 by number) |
| $Z$ | Gas metallicity in units of solar metallicity |
