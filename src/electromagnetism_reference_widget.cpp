#include "widgets/electromagnetism_reference_widget.h"

#include <QColor>
#include <QFrame>
#include <QEvent>
#include <QPalette>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QTextBrowser *makeBrowser(QWidget *parent, const QString &html, const QPalette &palette)
{
    auto *browser = new QTextBrowser(parent);
    browser->setOpenExternalLinks(true);
    browser->setFrameShape(QFrame::NoFrame);
    browser->setAutoFillBackground(true);
    browser->setPalette(palette);
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    browser->setStyleSheet(QStringLiteral("QTextBrowser { background-color: %1; color: %2; border: 0px; }")
                               .arg(base.name(QColor::HexRgb), text.name(QColor::HexRgb)));
    browser->setHtml(html);
    return browser;
}

QString pageStyle(const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Base);
    const bool dark = base.lightness() < 128;
    const QColor text = dark ? QColor(238, 241, 244) : QColor(28, 32, 37);
    const QColor muted = dark ? QColor(189, 195, 202) : QColor(78, 84, 91);
    const QColor panel = dark ? QColor(41, 43, 47) : QColor(240, 243, 247);
    const QColor panel2 = dark ? QColor(48, 51, 56) : QColor(231, 236, 242);
    const QColor border = dark ? QColor(78, 82, 89) : QColor(174, 181, 190);
    QColor accent = palette.color(QPalette::Highlight);
    if (!accent.isValid() || (dark && accent.lightness() < 105))
        accent = dark ? QColor(92, 178, 255) : QColor(0, 112, 210);
    const QColor secondary = dark ? accent.lighter(125) : accent.darker(115);

    return QStringLiteral(R"HTML(
<style type="text/css">
html, body { font-family: sans-serif; line-height: 1.45; margin: 0; padding: 0; color: %1 !important; background-color: %2 !important; }
body { margin: 18px; }
p, li, span, td, th { color: %1 !important; }
h1 { color: %3 !important; margin-top: 0; }
h2, h3 { color: %4 !important; margin-top: 24px; }
.eq { font-family: serif; font-size: 16px; color: %1 !important; background-color: %5 !important; border-left: 4px solid %3; padding: 8px 12px; margin: 8px 0; }
table { border-collapse: collapse; width: 100%; color: %1 !important; background-color: %2 !important; }
th, td { border: 1px solid %7; padding: 7px 9px; text-align: left; color: %1 !important; background-color: %2 !important; }
th { background-color: %6 !important; color: %1 !important; font-weight: 700; }
tr:nth-child(even) td { background-color: %5 !important; }
.note { color: %1 !important; background-color: %5 !important; border-left: 4px solid %4; padding: 8px 12px; }
.small { color: %8 !important; }
.vecsym { color: %3 !important; font-weight: 700; }
.scalarsym { font-style: italic; font-weight: 600; }
.typebadge { display: inline-block; border: 1px solid %7; background-color: %5 !important; padding: 1px 6px; }
a { color: %3 !important; }
</style>)HTML")
        .arg(text.name(QColor::HexRgb), base.name(QColor::HexRgb), accent.name(QColor::HexRgb),
             secondary.name(QColor::HexRgb), panel.name(QColor::HexRgb), panel2.name(QColor::HexRgb),
             border.name(QColor::HexRgb), muted.name(QColor::HexRgb));
}
}


ElectromagnetismReferenceWidget::ElectromagnetismReferenceWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);
    rebuildPages();
}

void ElectromagnetismReferenceWidget::rebuildPages()
{
    if (!m_tabs) return;
    const QString currentTabText =
        (m_tabs->currentIndex() >= 0) ? m_tabs->tabText(m_tabs->currentIndex()) : QString();
    while (m_tabs->count() > 0)
    {
        QWidget *page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete page;
    }
    auto *tabs = m_tabs;
    const QString style = pageStyle(palette());

    tabs->addTab(makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>Physical constants</h1>
<table>
<tr><th>Constant</th><th>Symbol</th><th>Value used by the app</th><th>Unit</th></tr>
<tr><td>Vacuum permittivity</td><td>ε₀</td><td>8.854 187 8128 × 10⁻¹²</td><td>F/m</td></tr>
<tr><td>Vacuum permeability</td><td>μ₀</td><td>1.256 637 0614 × 10⁻⁶</td><td>H/m</td></tr>
<tr><td>Coulomb constant</td><td>k = 1/(4π ε₀)</td><td>8.987 551 7923 × 10⁹</td><td>N·m²/C²</td></tr>
<tr><td>Biot–Savart factor</td><td>μ₀/(4π)</td><td>1.0 × 10⁻⁷</td><td>T·m/A</td></tr>
<tr><td>Speed of light</td><td>c</td><td>299 792 458</td><td>m/s</td></tr>
<tr><td>Elementary charge</td><td>e</td><td>1.602 176 634 × 10⁻¹⁹</td><td>C</td></tr>
</table>
<h2>Useful identities</h2>
<div class="eq">c = 1 / √(μ₀ ε₀)</div>
<div class="eq">D = ε₀ E &nbsp;&nbsp; and &nbsp;&nbsp; B = μ₀ H &nbsp; (vacuum)</div>
<div class="eq">F = q (E + v × B)</div>
)HTML"), palette()), QStringLiteral("Constants"));

    tabs->addTab(makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>Electrostatics</h1>
<h2>Coulomb law — discrete charges</h2>
<div class="eq">Rᵢ = r_M − rᵢ, &nbsp; E(M) = (1 / 4π ε₀) Σ qᵢ Rᵢ / |Rᵢ|³</div>
<div class="eq">F = q_test E</div>
<h2>Charge densities</h2>
<table>
<tr><th>Distribution</th><th>Density</th><th>Charge element</th><th>Unit</th></tr>
<tr><td>Line</td><td>λ = dQ/dℓ</td><td>dQ = λ dℓ</td><td>C/m</td></tr>
<tr><td>Surface</td><td>σ = dQ/dS</td><td>dQ = σ dS</td><td>C/m²</td></tr>
<tr><td>Volume</td><td>ρ = dQ/dV</td><td>dQ = ρ dV</td><td>C/m³</td></tr>
</table>
<div class="eq">Q = ∫<sub>C</sub> λ dℓ, &nbsp; Q = ∬<sub>S</sub> σ dS, &nbsp; Q = ∭<sub>V</sub> ρ dV</div>
<h2>Electric field for continuous distributions</h2>
<div class="eq">R = r_M − r′</div>
<div class="eq">Line: &nbsp; E(M) = (1 / 4π ε₀) ∫<sub>C</sub> λ(r′) R / |R|³ dℓ′</div>
<div class="eq">Surface: &nbsp; E(M) = (1 / 4π ε₀) ∬<sub>S</sub> σ(r′) R / |R|³ dS′</div>
<div class="eq">Volume: &nbsp; E(M) = (1 / 4π ε₀) ∭<sub>V</sub> ρ(r′) R / |R|³ dV′</div>
<h2>Electric potential — discrete and continuous</h2>
<div class="eq">V(M) = (1 / 4π ε₀) Σ qᵢ / |Rᵢ|</div>
<div class="eq">V(M) = (1 / 4π ε₀) ∫ λ(r′)/|R| dℓ′</div>
<div class="eq">V(M) = (1 / 4π ε₀) ∬ σ(r′)/|R| dS′</div>
<div class="eq">V(M) = (1 / 4π ε₀) ∭ ρ(r′)/|R| dV′</div>
<div class="eq">E = −∇V</div>
<h2>Cylindrical and spherical integration elements</h2>
<div class="eq">Cylindrical (r, φ, z): &nbsp; dV = r dr dφ dz</div>
<div class="eq">Cylinder r = R: &nbsp; dS = R dφ dz</div>
<div class="eq">Spherical (r, θ, φ): &nbsp; dV = r² sinθ dr dθ dφ</div>
<div class="eq">Sphere r = R: &nbsp; dS = R² sinθ dθ dφ</div>
<h2>Gauss law</h2>
<div class="eq">∯<sub>∂V</sub> E · n dS = Q_enclosed / ε₀</div>
<div class="eq">∇ · E = ρ / ε₀</div>
<h2>Common symmetric distributions</h2>
<div class="eq">Infinite line charge λ: &nbsp; E(r) = [λ / (2π ε₀ r)] r̂</div>
<div class="eq">Infinite plane charge σ: &nbsp; E = [σ / (2 ε₀)] n̂ on the chosen side</div>
<div class="eq">Uniform infinite solid cylinder ρ, r&lt;R: &nbsp; E(r) = [ρ r / (2 ε₀)] r̂</div>
<div class="eq">Uniform infinite solid cylinder ρ, r≥R: &nbsp; E(r) = [ρ R² / (2 ε₀ r)] r̂</div>
<div class="eq">Uniform solid sphere ρ, r&lt;R: &nbsp; E(r) = [ρ r / (3 ε₀)] r̂</div>
<div class="eq">Uniform solid sphere ρ, r≥R: &nbsp; E(r) = [ρ R³ / (3 ε₀ r²)] r̂ = [Q / (4π ε₀ r²)] r̂</div>
<div class="eq">Uniform spherical shell σ, r&lt;R: &nbsp; E = 0; &nbsp; r≥R: E(r) = [σ R² / (ε₀ r²)] r̂</div>
<h2>Hollow volume distributions — 5.19 study models</h2>
<div class="eq">Thick spherical shell a&lt;r&lt;R, uniform ρ: &nbsp; E(r) = [ρ/(3ε₀)] [r − a³/r²] r̂</div>
<div class="eq">Thick spherical shell r&lt;a: &nbsp; E = 0; &nbsp; r≥R: E = Q/(4π ε₀ r²) r̂</div>
<div class="eq">Q = (4π/3)ρ(R³−a³)</div>
<div class="eq">Infinite hollow cylinder, uniform annular charge and total λ per metre: &nbsp; r&lt;a → E=0</div>
<div class="eq">a≤r&lt;R: &nbsp; E(r) = λ(r²−a²) / [2π ε₀ r(R²−a²)] r̂; &nbsp; r≥R: E(r)=λ/(2π ε₀ r) r̂</div>
<h2>Finite plate / slab study models</h2>
<div class="eq">Uniform finite-thickness rectangular slab: &nbsp; ρ = Q/(W H t)</div>
<div class="eq">E(r) = [1/(4π ε₀)] ∭<sub>slab</sub> ρ (r−r′)/|r−r′|³ dV′</div>
<div class="eq">V(r) = [1/(4π ε₀)] ∭<sub>slab</sub> ρ / |r−r′| dV′</div>
<div class="eq">Uniform disk, on axis z&gt;0: &nbsp; E_z = [σ/(2ε₀)] [1 − z/√(z²+R²)]</div>
<div class="eq">Annular disk on axis: subtract the inner-disk contribution from the outer-disk contribution.</div>
<p class="note">Infinitesimally thin rectangular, circular, annular and triangular plates are evaluated by symmetric numerical surface integration away from singular source points. The finite-thickness rectangular plate is a uniformly charged volume and uses tensor-product Gauss-Legendre volume integration. The circular/annular integrator uses equal-area polar cells; the triangular source is an isosceles triangle centred at its centroid.</p>
<h2>Finite uniformly charged wire — useful validation case</h2>
<p>For a wire of total charge Q, length L, centred on the X axis, the application uses λ = Q/L. At the perpendicular bisector M = (0,y,0):</p>
<div class="eq">Eₓ = E_z = 0</div>
<div class="eq">E_y = k Q / [ y √(y² + (L/2)²) ]</div>
<div class="eq">V = 2 k (Q/L) asinh[L/(2y)]</div>
<p class="note">The point-charge value kQ/y² is only the far-field approximation when L ≪ y. For Q = 1 C, L = 1 m and y = 1 m, the finite-line result is about 8.0387 GV/m, not 8.9876 GV/m.</p>
<h2>Poisson and Laplace equations</h2>
<div class="eq">∇²V = −ρ / ε₀</div>
<div class="eq">Charge-free region (ρ = 0): &nbsp; ∇²V = 0</div>
<p class="note">Finite uniformly charged wires are evaluated with an exact closed-form line integral. Rectangular plates still use symmetric midpoint surface integration so edge effects remain visible. The sphere, shell and infinite-cylinder cases use their stated analytical symmetry relations.</p>
)HTML"), palette()), QStringLiteral("Electrostatics"));

    tabs->insertTab(0, makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>Vector calculus for electromagnetism</h1>
<p class="note"><b>Visual notation used on this page.</b> Scalars are written in <span class="scalarsym">italic</span>. Vector quantities use a visible arrow and accent colour, for example <span class="vecsym">A⃗</span>, and their Cartesian component form is written as a column vector. The nabla symbol ∇ is <b>not a scalar</b>: it is a formal vector differential operator.</p>

<h2>Scalar, vector and tensor objects</h2>
<table>
<tr><th>Object</th><th>Typical notation</th><th>Mathematical type</th><th>Example</th></tr>
<tr><td>Scalar</td><td><span class="scalarsym">a, f, V</span></td><td>One value, ℝ</td><td>Electric potential V</td></tr>
<tr><td>Vector</td><td><span class="vecsym">A⃗</span> = [Aₓ, Aᵧ, A_z]ᵀ</td><td>3 components, ℝ³</td><td><span class="vecsym">E⃗</span>, <span class="vecsym">B⃗</span>, <span class="vecsym">J⃗</span></td></tr>
<tr><td>Second-order tensor / matrix</td><td>J<sub>A</sub> = [∂Aᵢ/∂xⱼ]</td><td>3 × 3 components, ℝ³ˣ³</td><td>Jacobian of a vector field</td></tr>
</table>

<h2>Vector algebra — operation types</h2>
<table>
<tr><th>Operation</th><th>Symbol</th><th>Inputs</th><th>Output</th><th>Meaning / formula</th></tr>
<tr><td>Scalar multiplication</td><td>α<span class="vecsym">A⃗</span></td><td>Scalar × vector</td><td><span class="vecsym">Vector</span></td><td>Scales the vector magnitude; reverses direction if α &lt; 0</td></tr>
<tr><td>Dot product / scalar product</td><td><span class="vecsym">A⃗</span> · <span class="vecsym">B⃗</span></td><td>Vector · vector</td><td><span class="scalarsym">Scalar</span></td><td>AₓBₓ + AᵧBᵧ + A_zB_z = |A||B| cosθ</td></tr>
<tr><td>Cross product / vector product</td><td><span class="vecsym">A⃗</span> × <span class="vecsym">B⃗</span></td><td>Vector × vector</td><td><span class="vecsym">Vector</span></td><td>Perpendicular to A and B; magnitude |A||B| sinθ; direction by right-hand rule</td></tr>
<tr><td>Norm</td><td>||<span class="vecsym">A⃗</span>||</td><td>Vector</td><td><span class="scalarsym">Scalar</span></td><td>√(<span class="vecsym">A⃗</span>·<span class="vecsym">A⃗</span>)</td></tr>
<tr><td>Outer / dyadic product</td><td><span class="vecsym">A⃗</span> ⊗ <span class="vecsym">B⃗</span></td><td>Vector ⊗ vector</td><td>Tensor / 3×3 matrix</td><td>(A⊗B)ᵢⱼ = AᵢBⱼ</td></tr>
</table>
<div class="eq"><span class="vecsym">A⃗</span> · <span class="vecsym">B⃗</span> = <span class="vecsym">B⃗</span> · <span class="vecsym">A⃗</span> &nbsp; (commutative)</div>
<div class="eq"><span class="vecsym">A⃗</span> × <span class="vecsym">B⃗</span> = −(<span class="vecsym">B⃗</span> × <span class="vecsym">A⃗</span>) &nbsp; (anti-commutative)</div>
<div class="eq">If <span class="vecsym">A⃗</span> ⟂ <span class="vecsym">B⃗</span>: &nbsp; <span class="vecsym">A⃗</span>·<span class="vecsym">B⃗</span> = 0</div>
<div class="eq">If <span class="vecsym">A⃗</span> ∥ <span class="vecsym">B⃗</span>: &nbsp; <span class="vecsym">A⃗</span>×<span class="vecsym">B⃗</span> = <span class="vecsym">0⃗</span></div>

<h2>Nabla and basic differential operators — Cartesian coordinates</h2>
<div class="eq">∇ = x̂ ∂/∂x + ŷ ∂/∂y + ẑ ∂/∂z</div>
<div class="eq">Scalar field f : ℝ³ → ℝ</div>
<div class="eq">Gradient: &nbsp; ∇f = ( ∂f/∂x , ∂f/∂y , ∂f/∂z )ᵀ &nbsp; ∈ ℝ³</div>
<div class="eq"><span class="vecsym">grad f</span> ≡ ∇f = [ ∂f/∂x, ∂f/∂y, ∂f/∂z ]ᵀ &nbsp; — vector output</div>
<div class="eq">Vector field A = (Aₓ, Aᵧ, A_z)ᵀ : ℝ³ → ℝ³</div>
<div class="eq">Divergence: &nbsp; ∇·A = ∂Aₓ/∂x + ∂Aᵧ/∂y + ∂A_z/∂z &nbsp; ∈ ℝ</div>
<div class="eq">Curl: &nbsp; ∇×A = ( ∂A_z/∂y − ∂Aᵧ/∂z , &nbsp; ∂Aₓ/∂z − ∂A_z/∂x , &nbsp; ∂Aᵧ/∂x − ∂Aₓ/∂y )ᵀ &nbsp; ∈ ℝ³</div>
<div class="eq">Scalar Laplacian: &nbsp; ∇²f = ∇·(∇f) = ∂²f/∂x² + ∂²f/∂y² + ∂²f/∂z² &nbsp; ∈ ℝ</div>
<div class="eq">Vector Laplacian (Cartesian): &nbsp; ∇²A = (∇²Aₓ, ∇²Aᵧ, ∇²A_z)ᵀ &nbsp; ∈ ℝ³</div>
<div class="eq">Jacobian of a vector field: &nbsp; J_A = [ ∂Aᵢ / ∂xⱼ ] &nbsp; ∈ ℝ³ˣ³</div>

<h2>Mathematical interpretation</h2>
<table>
<tr><th>Operation</th><th>Input</th><th>Operator action</th><th>Output</th></tr>
<tr><td>Gradient ∇f</td><td><span class="scalarsym">Scalar field f</span></td><td>The vector differential operator differentiates the same scalar along x, y and z</td><td><span class="vecsym">Vector field</span> (3 components)</td></tr>
<tr><td>Divergence ∇·A</td><td><span class="vecsym">Vector field A⃗</span></td><td>Dot contraction of ∇ with A</td><td><span class="scalarsym">Scalar field</span></td></tr>
<tr><td>Curl ∇×A</td><td><span class="vecsym">Vector field A⃗</span></td><td>Cross/antisymmetric contraction of ∇ with A</td><td><span class="vecsym">Vector field</span> (3 components)</td></tr>
<tr><td>Scalar Laplacian ∇²f</td><td><span class="scalarsym">Scalar field f</span></td><td>Divergence of the gradient: ∇·(∇f)</td><td><span class="scalarsym">Scalar field</span></td></tr>
<tr><td>Jacobian J_A</td><td><span class="vecsym">Vector field A⃗</span></td><td>All first partial derivatives ∂Aᵢ/∂xⱼ, without contraction</td><td>Second-order tensor / 3×3 matrix</td></tr>
<tr><td>Vector Laplacian ∇²A</td><td><span class="vecsym">Vector field A⃗</span></td><td>Scalar Laplacian applied componentwise in Cartesian coordinates</td><td><span class="vecsym">Vector field</span></td></tr>
</table>
<p class="small">The gradient does <b>not</b> take a vector field as its standard input: it maps a scalar field to a vector field. Differentiating every component of a vector field without contracting the derivatives gives the Jacobian tensor.</p>

<h2>Physical interpretation</h2>
<table>
<tr><th>Operator</th><th>Interpretation</th><th>EM example</th></tr>
<tr><td>∇f</td><td>Direction of steepest increase; magnitude gives the maximum local rate of increase</td><td><span class="vecsym">E⃗</span> = −∇V</td></tr>
<tr><td>∇·A</td><td>Local source/sink density or net outward flux per unit volume</td><td>∇·<span class="vecsym">E⃗</span> = ρ/ε₀</td></tr>
<tr><td>∇×A</td><td>Local circulation axis and rotational tendency</td><td>∇×<span class="vecsym">E⃗</span> = −∂<span class="vecsym">B⃗</span>/∂t</td></tr>
<tr><td>∇²f</td><td>Local curvature relative to neighboring values; appears in diffusion and potential equations</td><td>∇²V = −ρ/ε₀</td></tr>
</table>

<h2>Gauss–Ostrogradsky divergence theorem</h2>
<div class="eq">∭<sub>V</sub> (∇·A) dV = ∯<sub>∂V</sub> A · n̂ dS</div>
<p>It converts a volume integral of divergence into the outward flux through the closed boundary. Gauss's electrostatic law is the direct electromagnetic application.</p>

<h2>Stokes theorem</h2>
<div class="eq">∬<sub>S</sub> (∇×A) · n̂ dS = ∮<sub>∂S</sub> A · dℓ</div>
<p>It connects local curl to circulation around the contour. Faraday and Ampère–Maxwell laws are its canonical EM applications.</p>

<h2>Green identities</h2>
<div class="eq">First identity: &nbsp; ∭<sub>V</sub> (∇u·∇v + u∇²v) dV = ∯<sub>∂V</sub> u ∂v/∂n dS</div>
<div class="eq">Second identity: &nbsp; ∭<sub>V</sub> (u∇²v − v∇²u) dV = ∯<sub>∂V</sub> (u ∂v/∂n − v ∂u/∂n) dS</div>
<div class="eq">Normal derivative: &nbsp; ∂v/∂n = ∇v · n̂</div>
<p class="note">Green identities are fundamental for potential theory and underlie boundary-element formulations of Poisson/Laplace problems.</p>

<h2>Useful vector identities</h2>
<div class="eq">∇×(∇f) = 0</div>
<div class="eq">∇·(∇×A) = 0</div>
<div class="eq">∇×(∇×A) = ∇(∇·A) − ∇²A</div>
<div class="eq">∇·(fA) = ∇f·A + f ∇·A</div>
<div class="eq">∇×(fA) = ∇f×A + f ∇×A</div>

<h2>Coordinate-system reminders</h2>
<div class="eq">Cylindrical: &nbsp; dV = r dr dφ dz</div>
<div class="eq">Spherical: &nbsp; dV = r² sinθ dr dθ dφ</div>
<div class="eq">Cylindrical scalar Laplacian: &nbsp; ∇²f = (1/r)∂/∂r(r∂f/∂r) + (1/r²)∂²f/∂φ² + ∂²f/∂z²</div>
<div class="eq">Spherical scalar Laplacian: &nbsp; ∇²f = (1/r²)∂/∂r(r²∂f/∂r) + [1/(r² sinθ)]∂/∂θ(sinθ ∂f/∂θ) + [1/(r² sin²θ)]∂²f/∂φ²</div>

<h2>Potential formulations used in EM</h2>
<div class="eq">Electrostatics: &nbsp; E = −∇V &nbsp; ⇒ &nbsp; ∇×E = 0</div>
<div class="eq">Poisson: &nbsp; ∇²V = −ρ/ε₀; &nbsp; Laplace: ∇²V = 0 where ρ = 0</div>
<div class="eq">Magnetostatics: &nbsp; B = ∇×A &nbsp; ⇒ &nbsp; ∇·B = 0</div>
<div class="eq">Coulomb gauge ∇·A = 0: &nbsp; ∇²A = −μ₀J</div>
)HTML"), palette()), QStringLiteral("Vector maths"));


    tabs->addTab(makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>Magnetostatics</h1>
<h2>Biot–Savart law</h2>
<div class="eq">dB = (μ₀ / 4π) I (dℓ × r) / r³</div>
<h2>Ampère law</h2>
<div class="eq">∮ B · dℓ = μ₀ I_enclosed</div>
<div class="eq">∇ × B = μ₀ J &nbsp; (magnetostatic case)</div>
<h2>Infinite straight wire</h2>
<div class="eq">B(r) = μ₀ I / (2πr), direction given by the right-hand rule</div>
<h2>Finite straight wire — perpendicular bisector</h2>
<div class="eq">B(ρ) = [μ₀ I / (4πρ)] · L / √(ρ² + (L/2)²)</div>
<p class="note">The finite straight-wire source now uses the exact Biot–Savart segment expression for arbitrary 3D orientation, so symmetry-forbidden components are not introduced by numerical sampling.</p>
<h2>Circular loop on its axis</h2>
<div class="eq">B(z) = μ₀ I R² / [2 (R² + z²)^(3/2)]</div>
<div class="eq">At loop center: B = μ₀ I / (2R)</div>
<h2>Ideal long solenoid</h2>
<div class="eq">B ≈ μ₀ n I, with n = N/L</div>
<h2>Uniform current cylinders — 5.19 study models</h2>
<div class="eq">Solid cylinder, uniform J, r&lt;R: &nbsp; B(r)=μ₀ I r/(2πR²) φ̂; &nbsp; r≥R: B=μ₀I/(2πr) φ̂</div>
<div class="eq">Hollow cylinder a&lt;r&lt;R: &nbsp; B(r)=μ₀ I(r²−a²)/[2πr(R²−a²)] φ̂; &nbsp; r&lt;a: B=0</div>
<h2>Helmholtz pair</h2>
<div class="eq">Two equal coaxial loops separated by s: &nbsp; B_center = μ₀ I R² / [R² + (s/2)²]^(3/2)</div>
<div class="eq">For the Helmholtz condition s=R: &nbsp; B_center = (4/5)^(3/2) μ₀ I / R</div>
<h2>Infinite current sheet</h2>
<div class="eq">|B| = μ₀ K / 2 on each side; directions are opposite and follow K × n̂.</div>
<p class="note">Rectangular and triangular current loops are assembled from exact finite straight-segment Biot–Savart contributions, so they are useful for hand-calculation exercises and superposition checks.</p>
<h2>Magnetic dipole</h2>
<div class="eq">B = (μ₀ / 4πr³) [3(m · r̂)r̂ − m]</div>
<h2>Lorentz force</h2>
<div class="eq">F_m = q v × B</div>
<div class="eq">dF = I dℓ × B</div>
<p class="note">The finite straight wire uses the exact Biot–Savart segment expression. Circular loops use segmented Biot–Savart integration. Finite solenoids use midpoint-distributed virtual loops, which strongly improves agreement with the standard on-axis finite-solenoid result while retaining an off-axis educational model. The displayed H assumes vacuum: H = B/μ₀.</p>
)HTML"), palette()), QStringLiteral("Magnetostatics"));

    tabs->addTab(makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>Maxwell equations</h1>
<h2>Differential form</h2>
<table>
<tr><th>Law</th><th>Equation</th></tr>
<tr><td>Gauss — electricity</td><td>∇ · E = ρ / ε₀</td></tr>
<tr><td>Gauss — magnetism</td><td>∇ · B = 0</td></tr>
<tr><td>Faraday</td><td>∇ × E = −∂B/∂t</td></tr>
<tr><td>Ampère–Maxwell</td><td>∇ × B = μ₀J + μ₀ε₀ ∂E/∂t</td></tr>
</table>
<h2>Integral form</h2>
<div class="eq">∯<sub>S</sub> E · n̂ dS = Q_enclosed / ε₀</div>
<div class="eq">∯<sub>S</sub> B · n̂ dS = 0</div>
<div class="eq">∮<sub>C</sub> E · dℓ = − d/dt ∬<sub>S</sub> B · n̂ dS</div>
<div class="eq">∮<sub>C</sub> B · dℓ = μ₀ I_enclosed + μ₀ε₀ d/dt ∬<sub>S</sub> E · n̂ dS</div>
<h2>Static limits used in this application</h2>
<div class="eq">Electrostatics: ∂/∂t = 0 → ∇ × E = 0</div>
<div class="eq">Magnetostatics: ∂/∂t = 0 → ∇ × B = μ₀J</div>
<p class="note">The electrostatic and magnetostatic canvases remain static-field solvers. QTsignalApp 2.0 keeps the electrostatic and magnetostatic canvases as static-field solvers, while RF / EM / composants → Numerical solvers contains a separate 2D TMz/TEz FDTD time-domain Maxwell solver with CPML and RF analysis.</p>
)HTML"), palette()), QStringLiteral("Maxwell"));

    tabs->addTab(makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>RF, antennas and passive components</h1>
<h2>Lossless plane wave</h2>
<div class="eq">v = 1 / √(με), &nbsp; λ = v/f, &nbsp; η = √(μ/ε), &nbsp; β = 2π/λ</div>
<div class="eq">H = k̂ × E / η, &nbsp; B = μH</div>
<div class="eq">&lt;S&gt; = E₀² / (2η) k̂ &nbsp; for peak E₀</div>
<h2>Short antennas</h2>
<div class="eq">Hertzian element: Rᵣ = 80π² (ℓ/λ)²</div>
<div class="eq">Short center-fed dipole: Rᵣ ≈ 20π² (L/λ)²</div>
<div class="eq">Small loop: Rᵣ ≈ 31 200 (N A / λ²)²</div>
<h3>Inductive / magnetic-loop antennas</h3>
<div class="eq">Magnetic dipole moment: m = N I A</div>
<div class="eq">Circular-loop axial field: B_z(z) = μ₀ N I r² / [2(r²+z²)^(3/2)]</div>
<div class="eq">Reactive-near-field scale: r ≲ λ / (2π)</div>
<div class="eq">Series coil: Z_s ≈ R_ac + R_r + jωL ; with C_p: Z_in = [1/Z_s + jωC_p]⁻¹</div>
<div class="eq">Self-resonance estimate: f_SRF ≈ 1 / [2π√(L C_p)]</div>
<div class="eq">Circular planar spiral (Mohan): L ≈ μ₀N²d_avg/2 [ln(2.46/ρ_f) + 0.20ρ_f²], &nbsp; ρ_f=(d_out−d_in)/(d_out+d_in)</div>
<p class="note">These loop/solenoid relations are electrically-small / quasi-static engineering models. Near self resonance or when the circumference is no longer ≪ λ, distributed capacitance and full-wave current distribution must be included.</p>
<h2>Inductance</h2>
<div class="eq">Ideal solenoid: L = μ N² A / ℓ</div>
<div class="eq">Stored magnetic energy: W = ½ L I²</div>
<div class="eq">Coupled coils: M = k √(L₁L₂)</div>
<h2>Capacitance and dielectric loss</h2>
<div class="eq">Homogeneous dielectric: ε = ε₀ εᵣ</div>
<div class="eq">Parallel plates: C = ε A / d</div>
<div class="eq">Coaxial cylinders: C' = 2π ε / ln(b/a)</div>
<div class="eq">Concentric spheres: C = 4π ε ab / (b-a)</div>
<div class="eq">With loss tangent tanδ: Y ≈ ωC tanδ + jωC, &nbsp; Z = 1/Y, &nbsp; Q_d ≈ 1/tanδ</div>
<p class="note">The dielectric presets are editable engineering starting values. εᵣ and tanδ depend on frequency, temperature, material formulation and manufacturing. The closed forms assume the region between conductors is filled by one homogeneous dielectric.</p>
<h2>Transformer / coupled-inductor input impedance</h2>
<div class="eq">M = k√(L₁L₂), &nbsp; Zin = R₁ + jωL₁ + ω²M² / (ZL + R₂ + jωL₂)</div>
<h3>Shared magnetic-core approximation</h3>
<div class="eq">ℜ = lₑ/(μ₀ μᵣ Aₑ) + g/(μ₀ Aₑ)</div>
<div class="eq">A_L = 1/ℜ, &nbsp; L₁=N₁²A_L, &nbsp; L₂=N₂²A_L</div>
<div class="eq">I_sat,1 ≈ B_sat ℜ Aₑ / N₁</div>
<p class="note">This is a lumped magnetic-circuit model. Real transformers additionally require complex/frequency-dependent permeability, core-loss data, fringing around gaps, leakage-field geometry, winding capacitance and skin/proximity effects.</p>
<h3>Common-mode choke / coupled-inductor modes</h3>
<div class="eq">L-matrix = [[L₁,M],[M,L₂]]</div>
<div class="eq">L± = ½[(L₁+L₂) ± √((L₁−L₂)²+4M²)]</div>
<div class="eq">Equal windings: L_CM = L+M, &nbsp; L_DM = L−M</div>
<p class="note">For a bifilar common-mode choke with equal turns and consistent dot polarity, common-mode currents reinforce core flux while differential currents largely cancel it. The eigenmode expression remains valid when L₁ and L₂ differ, although the eigenvectors are then not exactly equal-current common/differential modes.</p>
<h3>BALUN / UNUN first-order relations</h3>
<div class="eq">n = N₂/N₁, &nbsp; Z₂/Z₁ = n², &nbsp; Z_load,referred = Z_load/n²</div>
<div class="eq">Z_choke ≈ R_w + jωL_m, &nbsp; f_SRF ≈ 1/(2π√(L_m C_p))</div>
<div class="eq">Γ = (Z_in−Z₀)/(Z_in+Z₀), &nbsp; VSWR=(1+|Γ|)/(1−|Γ|)</div>
<p class="note">Guanella and Ruthroff baluns are distributed transmission-line transformers in real hardware. The BALUN/UNUN calculator uses their ideal impedance ratio plus a lumped magnetizing/leakage/parasitic equivalent for first-order sizing; broadband accuracy requires transmission-line Z₀, electrical length and core-loss modelling.</p>
<h2>RF network analysis</h2>
<div class="eq">Γ = S₁₁, &nbsp; Z_in = Z₀ (1 + Γ) / (1 − Γ)</div>
<div class="eq">Return loss = −20 log₁₀|S₁₁|, &nbsp; VSWR = (1 + |Γ|)/(1 − |Γ|)</div>
<div class="eq">τ_g = −dφ₂₁/dω</div>
<h2>Cascaded antenna matching / RF chain</h2>
<div class="eq">Γ_source(f) = [Z_source(f) − Z_ref] / [Z_source(f) + Z_ref]</div>
<div class="eq">Source → PCB line → L-match / optional shunt trap → antenna Z_A(f)</div>
<div class="eq">Series reactive element: Z_s = jX_s ; &nbsp; shunt reactive element: Y_p = jB_p</div>
<div class="eq">X_s&gt;0 → L_s=X_s/ω ; X_s&lt;0 → C_s=−1/(ωX_s)</div>
<div class="eq">B_p&gt;0 → C_p=B_p/ω ; B_p&lt;0 → L_p=−1/(ωB_p)</div>
<h3>Two-element L-match synthesis at one frequency</h3>
<div class="eq">For source → series → shunt || load, write Y_L=G+jB_L:</div>
<div class="eq">B_tot = ±√(G/R₀ − G²), &nbsp; B_p=B_tot−B_L, &nbsp; X_s=−Im{1/(G+jB_tot)}</div>
<div class="eq">For source → shunt → series → load, with Z_L=R+jX_L:</div>
<div class="eq">X_tot = ±√(R R₀ − R²), &nbsp; X_s=X_tot−X_L, &nbsp; B_p=−Im{1/(R+jX_tot)}</div>
<p>The RF chain tab re-evaluates the fixed synthesized L/C values over frequency, so the exact match generally occurs only near the synthesis frequency. An imported Antenna Designer sweep supplies the complex Z_A(f) point-by-point/interpolated inside its solved frequency range.</p>
<h3>Non-ideal RF inductors and capacitors</h3>
<div class="eq">Inductor core branch: Z_Ls = R_s(f)+jωL, &nbsp; Y_L = 1/Z_Ls + jωC_p + 1/R_p, &nbsp; Z_L=1/Y_L</div>
<div class="eq">Capacitor dielectric branch: Y_C = ωC tanδ + jωC, &nbsp; Z_C = R_ESR + jωL_ESL + 1/Y_C</div>
<div class="eq">Q_eff(f) ≈ |Im Z|/|Re Z|, &nbsp; f_SRF,L ≈ 1/(2π√(LC_p)), &nbsp; f_SRF,C ≈ 1/(2π√(L_ESL C))</div>
<p class="note">The RF L/C model is a first-order lumped equivalent. The optional Q@fref term adds frequency-dependent loss using an editable R∝f^α trend; datasheet impedance/Q curves remain preferable for precision work close to SRF.</p>
<h3>Complex permeability and winding AC loss</h3>
<div class="eq">μ*(f) = μ′(f) − j μ″(f), &nbsp; tanδμ = μ″/μ′</div>
<div class="eq">Single-pole approximation: μ′ = 1 + (μs−1)/(1+(f/fc)²), &nbsp; μ″ = (μs−1)(f/fc)/(1+(f/fc)²)</div>
<div class="eq">For nominal L referenced at fref: L(f) ≈ Lref μ′(f)/μ′(fref), &nbsp; Rcore,series ≈ ω L(f) tanδμ</div>
<div class="eq">Round-wire skin depth: δ = √[ρ/(π f μ₀)]</div>
<div class="eq">First-order skin factor: Fskin ≈ √[1+(a/(2δ))²], &nbsp; Rac ≈ Rdc Fskin Fprox</div>
<p class="note">The proximity multiplier Fprox is explicitly empirical in this engineering model. Accurate multilayer winding loss requires geometry-aware Dowell/field modelling. The single-pole μ model is likewise a teaching approximation; manufacturer μ′/μ″ or complex impedance data should be used for quantitative ferrite design.</p>
<h3>Core heating — Steinmetz / manufacturer loss data</h3>
<div class="eq">Classical sinusoidal Steinmetz: Pᵥ = k f^α B̂^β &nbsp; [QTsignalApp uses f in Hz, B̂ in T and Pᵥ in W/m³]</div>
<div class="eq">P_core = Pᵥ Vₑ, &nbsp; R_eq,op = P_core / I_rms²</div>
<p class="note">Steinmetz loss is amplitude dependent and therefore is not the same thing as the linear small-signal Rcore≈ωL tanδμ model. Coefficients depend on material, temperature, waveform and the unit convention used by the manufacturer. QTsignalApp also accepts measured/manufacturer f-B-Pv points and interpolates locally in log(f)-log(B).</p>
<h3>Multilayer winding loss — Dowell-like estimate</h3>
<div class="eq">ξ = t_cond/δ, &nbsp; F_R ≈ (ξ/2)[(sinhξ+sinξ)/(coshξ−cosξ) + ((2m²−1)/3)(sinhξ−sinξ)/(coshξ+cosξ)]</div>
<div class="eq">R_ac ≈ R_dc F_skin F_prox F_R</div>
<p class="note">The classical Dowell derivation assumes 1-D field penetration in foil/layer windings. QTsignalApp exposes it as a Dowell-like engineering estimate for tightly packed multilayer windings; arbitrary round-wire proximity effects require geometry-aware field analysis or measured impedance.</p>
<h3>Datasheet / measured complex impedance</h3>
<div class="eq">Z(f_i) = R(f_i) + jX(f_i)</div>
<div class="eq">Between supplied points QTsignalApp interpolates R and X linearly versus log(f).</div>
<p class="note">The RF L/C page accepts semicolon rows f_MHz ; R_ohm ; X_ohm. Outside the supplied range the nearest endpoint is held only for visualization; this is not a physical extrapolation.</p>
<h3>Wideband L-match tuning</h3>
<div class="eq">J_mean = (1/N) Σ |Γ(f_i)|, &nbsp; J_worst = max_i |Γ(f_i)|</div>
<div class="eq">[L_s,C_p] or [C_s,L_p] → bounded coordinate search → min J over the requested band</div>
<p class="note">Wideband tuning keeps the synthesized L-match topology and component types fixed, then adjusts the two nominal values. Every candidate is evaluated through the full RF-chain model, including optional component parasitics, trap and feed line. It is a numerical tuning aid, not a guarantee of a globally optimum broadband matching network.</p>
<p class="note">A lossy feed line attenuates both the incident and reflected waves. Consequently the source-plane |S₁₁| can appear lower than the antenna-terminal |S₁₁| even when power is being dissipated in the line. Always inspect line loss and the antenna-terminal impedance as well as source-plane matching.</p>
<h2>PCB transmission lines</h2>
<div class="eq">Microstrip: Z₀ = Z₀(w/h, t/h, εr), &nbsp; v_p ≈ c/√ε_eff, &nbsp; β = 2πf/v_p</div>
<div class="eq">Skin depth: δ = √[2ρ/(ωμ₀)], &nbsp; surface resistance R_s = √(πf μ₀ρ)</div>
<div class="eq">Lossy terminated line: Z_in = Z₀ (Z_L + Z₀ tanh γℓ)/(Z₀ + Z_L tanh γℓ), &nbsp; γ=α+jβ</div>
<p class="note">The PCB trace calculator uses quasi-static closed forms for Z₀ plus first-order copper skin and dielectric-loss estimates. It is intended for stack-up sizing and frequency/length intuition; solder mask, copper roughness, anisotropy and high-frequency dispersion require a field-solver or fabrication-specific model.</p>
<h2>2nd-order RLC filters / antenna traps</h2>
<div class="eq">ω₀ = 2πf₀, &nbsp; f₀ = 1/(2π√LC), &nbsp; BW ≈ f₀/Q</div>
<div class="eq">Canonical RLC synthesis: L = Q R/ω₀, &nbsp; C = 1/(ω₀ Q R)</div>
<div class="eq">Series-LC shunt trap: C = 1/(ω₀²L), &nbsp; R_ESR ≈ ω₀L/Q</div>
<div class="eq">Magnitude response: |H(f)|_dB = 20 log₁₀|V_out/V_in|</div>
<p class="note">A notch trap at an antenna input is strongly affected by source/load impedance, inductor SRF, capacitor ESR/ESL and layout. The calculator therefore plots the complete selected RLC network instead of treating Q as a purely graphical parameter.</p>
<h2>RF link budget — Friis / free-space model</h2>
<div class="eq">Pᵣ = Pₜ Gₜ Gᵣ (λ / 4πR)² L &nbsp; (linear quantities)</div>
<div class="eq">FSPL(dB) = 20 log₁₀(4πR/λ)</div>
<div class="eq">EIRP(dBm) = Pₜ(dBm) + Gₜ(dBi) − Lₜₓ(dB)</div>
<div class="eq">Pᵣ(dBm) = EIRP − FSPL − L_misc + Gᵣ − Lᵣₓ</div>
<div class="eq">N₀ = kT, &nbsp; N(dBm) ≈ −173.975 + 10 log₁₀(B_Hz) + NF</div>
<div class="eq">SNR(dB) = Pᵣ − N, &nbsp; link margin = Pᵣ − receiver sensitivity</div>
<table>
<tr><th>Term</th><th>Meaning</th><th>Typical unit</th></tr>
<tr><td>Pₜ / Pᵣ</td><td>Transmitted / received power</td><td>dBm</td></tr>
<tr><td>Gₜ / Gᵣ</td><td>Transmit / receive antenna gain relative to isotropic</td><td>dBi</td></tr>
<tr><td>FSPL</td><td>Free-space spreading loss</td><td>dB</td></tr>
<tr><td>Lₜₓ, Lᵣₓ, L_misc</td><td>Cable, connector, polarization, atmospheric, obstacle, fading or implementation losses</td><td>dB</td></tr>
<tr><td>NF</td><td>Receiver noise figure</td><td>dB</td></tr>
<tr><td>B</td><td>Equivalent receiver noise bandwidth</td><td>Hz</td></tr>
</table>
<p class="note">Friis assumes free-space propagation and antennas operating in each other's far field. Near-field coupling, multipath and obstacles require a more detailed propagation model. QTsignalApp's Link budget tab keeps additional losses explicit instead of silently folding them into FSPL.</p>
<p>The Smith chart displays S₁₁ directly in the complex Γ plane; normalized impedance follows z=(1+Γ)/(1−Γ). QTsignalApp can detect local S₁₁ minima, estimate the S₂₁ −3 dB bandwidth and a VSWR≤2 matching band from a computed sweep.</p>
<h3>Normalized antenna radiation pattern</h3>
<div class="eq">E_n(θ,φ) = |E(θ,φ)| / max_{θ,φ}|E(θ,φ)|</div>
<div class="eq">E_{n,dB}(θ,φ) = 20 log₁₀ E_n(θ,φ)</div>
<p>Since 5.28, azimuth, elevation and 3D radiation views share one global normalization reference rather than normalizing each cut independently. The spherical convention is θ=0° toward +Z, θ=90° in the XY plane and φ=0° toward +X. The 3D color map is derived from the same globally normalized field. Directivity and gain are distinguished by G(θ,φ)=η<sub>rad</sub>D(θ,φ); for the standalone lossless wire model η<sub>rad</sub>=1 by model definition, while the hybrid path presently includes the modeled conductor efficiency. When a single feed/reference impedance is available, realized gain additionally includes the mismatch factor 1−|Γ|². The plotted beam direction is the maximum of the sampled angular grid plus the principal 2D cuts; reduce the angular step when a more precise direction is required.</p>
<h3>Antenna impedance sweep / sampled resonance</h3>
<div class="eq">Z_in(f) = V_feed(f) / I_feed(f)</div>
<div class="eq">Γ(f) = [Z_in(f) − Z₀] / [Z_in(f) + Z₀]</div>
<div class="eq">S₁₁,dB(f) = 20 log₁₀|Γ(f)|</div>
<div class="eq">f_match ≈ arg min_f |Γ(f)|, &nbsp; f_res ≈ arg min_f |Im{Z_in(f)}|</div>
<p>Since 5.40, the Antenna designer frequency sweep defaults to <b>Auto</b>: wire-only geometries use the generalized thin-wire MoM, while geometries containing PEC surfaces use the same Hybrid wire + PEC/RWG + dielectric/Sommerfeld input builder as the single-frequency Hybrid solve. The complete coupled system is rebuilt and solved at every sampled frequency; only far-field post-processing is disabled during the sweep. An explicit thin-wire legacy/fast mode remains available and clearly warns when it ignores PEC/dielectric geometry.</p>
<p class="note">The displayed best-match frequency, minimum-reactance frequency and VSWR≤2 bandwidth are discrete-grid estimates. Reduce the sweep span and increase the point count around a candidate resonance before treating the result as a precise design value.</p>
<h3>One-parameter antenna geometry optimization</h3>
<div class="eq">p* ≈ arg min<sub>p∈[pmin,pmax]</sub> J(p)</div>
<div class="eq">J_match(p) = |Γ(p)|, &nbsp; J_res(p) = |Im{Z_in(p)}| / Z₀</div>
<p>For every trial geometry factor p, QTsignalApp rebuilds the wire coordinates, re-meshes the conductor and solves the full MoM system at the target frequency. Available variables include a uniform XY dimension scale, the scale of the driven connected conductor, and the radial spacing of disconnected parasitic components.</p>
<p class="note">The optimizer is intentionally one-dimensional: a coarse interval scan selects a promising basin and a bounded golden-section refinement improves the candidate. It does not prove a global optimum. Verify mesh convergence and repeat with different bounds before using an optimized geometry as an engineering result.</p>
<p class="note">Touchstone .s1p export uses the solved S₁₁ directly. The optional .s2p export is only complete after explicitly assuming a symmetric reciprocal two-port, S₁₂=S₂₁ and S₂₂=S₁₁; the application warns before writing that file.</p>
<p class="note">Antenna input impedance outside the labelled simple models requires a full-wave method such as MoM/NEC/FEM. Arbitrary conductor capacitance similarly requires BEM/FEM rather than a closed-form expression.</p>
)HTML"), palette()), QStringLiteral("RF / passives"));

    tabs->addTab(makeBrowser(tabs, style + QStringLiteral(R"HTML(
<h1>Numerical methods</h1>
<h2>2D FDTD — TMz / TEz Yee grid</h2>
<div class="eq">TMz: ∂Hₓ/∂t = −(1/μ) ∂E_z/∂y ; ∂H_y/∂t = +(1/μ) ∂E_z/∂x</div>
<div class="eq">TMz: ∂E_z/∂t = (1/ε)(∂H_y/∂x − ∂H_x/∂y − σE_z)</div>
<div class="eq">TEz: ∂Eₓ/∂t = +(1/ε)∂H_z/∂y ; ∂E_y/∂t = −(1/ε)∂H_z/∂x</div>
<div class="eq">TEz: ∂H_z/∂t = (1/μ)(∂Eₓ/∂y − ∂E_y/∂x)</div>
<div class="eq">Δt ≤ 1 / [c √(1/Δx² + 1/Δy²)]</div>
<p>QTsignalApp staggers electric and magnetic updates on a 2D Yee-style grid and supports dielectric εr, μr, conductivity σ, PEC cells and multiple point sources/probes.</p>
<h3>CPML coordinate stretching</h3>
<div class="eq">∂/∂x → (1/κₓ) ∂/∂x + ψₓ , &nbsp; ψⁿ = b ψⁿ⁻¹ + c (∂F/∂x)ⁿ</div>
<p>The default outer boundary is a convolutional PML (CPML) with polynomial σ/κ grading. Mur order 1 and a PEC box remain available for comparison.</p>
<p class="note">TMz/TEz still assume ∂/∂z = 0. CPML performance is best when the absorber itself is homogeneous. Grid convergence and at least ~10–20 cells per shortest material wavelength remain necessary.</p>
<h3>Local plane-wave port estimate</h3>
<div class="eq">E⁺ = ½(E_t + ηH_t), &nbsp; E⁻ = ½(E_t − ηH_t), &nbsp; S₁₁ ≈ E⁻₁/E⁺₁</div>
<div class="eq">S₂₁ ≈ (E⁺₂/E⁺₁) √(η₁/η₂)</div>
<p class="note">This is an educational local field decomposition. It is reliable only where each probe samples a homogeneous, approximately single-mode wave normal to +X; it is not a calibrated waveguide modal port.</p>
<h3>2D PEC guide modal port</h3>
<div class="eq">f_c = m c₀ / [2 h √(εᵣ μᵣ)]</div>
<div class="eq">β = √(k² − (mπ/h)²), &nbsp; λg = 2π/β</div>
<div class="eq">C(x) = A⁺e^(−jβx) + A⁻e^(+jβx), &nbsp; S₁₁=A⁻in/A⁺in, &nbsp; S₂₁=A⁺out/A⁺in</div>
<p>QTsignalApp projects the full transverse scalar field on the selected parallel-plate guide mode at two neighboring X planes, then separates forward and backward modal amplitudes.</p>
<p class="note">TMz uses sine/Dirichlet modes m≥1; TEz uses cosine/Neumann modes m≥0. The current implementation assumes the same uniform aperture/material at both ports and is still a 2D model rather than a general 3D modal port.</p>
<h3>2D scalar Huygens near-to-far transform</h3>
<div class="eq">F(φ) ∝ ∮ [∂u/∂n − jk(n·r̂)u] e^(−jk r̂·r) ds</div>
<p>The harmonic scalar field u is E_z for TMz and H_z for TEz. The closed contour must lie in homogeneous space, enclose all radiators/scatterers and remain inside the absorbing boundary.</p>
<h2>2D electrostatic boundary-element method (BEM)</h2>
<div class="eq">V(r) = −(1 / 2π ε) ∮ σ(s′) ln(|r−r′|/r₀) ds′ + C</div>
<div class="eq">C′ = |Q′| / |V₁ − V₂|</div>
<p>The conductor boundaries are split into constant-charge panels. QTsignalApp solves the dense collocation system together with a zero-net-line-charge constraint, so the arbitrary logarithmic potential reference cancels for a two-conductor problem.</p>
<p class="note">This is a two-dimensional cross-section model: conductors are assumed infinitely long perpendicular to the drawing plane. The result is capacitance per unit length. End effects require a 3D BEM/FEM model.</p>
<h2>Thin-wire antenna Method of Moments</h2>
<div class="eq">−jωε Eᶦ_z = ∫ I(z′) [∂²/∂z² + k²] e^(−jkR)/(4πR) dz′</div>
<div class="eq">[Z][I] = [E], &nbsp; Z_in = V_gap / I_feed</div>
<p>The legacy numerical antenna tab applies Pocklington's EFIE to one straight center-fed dipole with pulse-current basis functions, point matching and a delta-gap excitation.</p>
<h3>Generalized wire geometry used by Antenna designer</h3>
<div class="eq">G(R) = e^(−jkR)/(4πR)</div>
<div class="eq">G̿_E = jωμ [ I̿ + (1/k²) ∇∇ ] G</div>
<div class="eq">Z_mn = t̂_m · ∫_(Δℓ_n) G̿_E(r_m,r′) · t̂_n dℓ′</div>
<div class="eq">[Z][I] = [E_gap]</div>
<div class="eq">Σ_(e∈J) s_(J,e) I_e = 0 &nbsp; for each T/Y/X junction J</div>
<div class="eq">C I = 0, &nbsp; I = T q, &nbsp; C T = 0</div>
<div class="eq">Tᵀ Z T q = Tᵀ E_gap</div>
<div class="eq">Legacy comparison: [ Z &nbsp; Cᵀ ; C &nbsp; 0 ] [ I ; λ_J ] = [ E_gap ; 0 ]</div>
<p>Each drawn metallic rod is split into straight pulse-current segments. Every segment interacts with every other segment through the free-space dyadic EFIE kernel, so disconnected parasitic rods are electromagnetically coupled to the driven element. For collinear source/observation segments the dyadic kernel reduces to the same scalar Pocklington kernel used by the straight-dipole solver.</p>
<p><b>Self / near-term quadrature (5.18):</b> the reactive Pocklington kernel changes very rapidly on the scale of the wire radius. A single Gauss panel over a segment several radii long can therefore produce a numerically converged linear solve but a strongly mesh-dependent input reactance. Pulse self and near-neighbour source segments are now integrated with radius-aware composite Gauss panels; far interactions retain the cheaper 16-point rule. This is a quadrature correction, not a change of the EFIE itself. The canonical half-wave dipole benchmark now uses two mesh levels near Δl/a≈4 and 3 to verify that Zin has actually stabilized.</p>
<p class="note">The delta-gap source and feed-current extraction were re-audited during this change. The pulse excitation remains an impressed axial field whose integrated gap voltage is the requested feed voltage, and the reported port current is the orientation-corrected current crossing the feed node.</p>
<p><b>Rooftop self / near quadrature (5.20):</b> Galerkin rooftop terms require a two-dimensional integral over both the observation and source spans. A fixed 4×4 rule was found to alias the sharply varying reactive self/near kernel, even for Δl/a of only a few units. Self and near support pairs are now subdivided on approximately the wire-radius scale on <i>both</i> spans; far interactions retain the compact 4×4 rule. This is a numerical quadrature correction only: the rooftop basis, thin-wire regularization and EFIE operator are unchanged.</p>
<div class="eq">Δℓ_target ≤ min(λ/N_λ, F_a a) &nbsp; (default F_a = 3.5)</div>
<p>The current implementation uses an adaptive mesh criterion based on both wavelength and wire radius. The designer exposes F_a as “Max Δl / radius” so mesh sensitivity can be studied explicitly. Coincident endpoints, endpoint-on-segment contacts and exact centerline crossings form common graph nodes. For degree&gt;2 T/Y/X nodes, the recommended formulation first builds the branch-incidence matrix C, removes linearly redundant KCL rows by rank analysis, then constructs a null-space current transform T. The solved current coefficients q therefore generate physical segment currents I that satisfy branch continuity by construction. The earlier Lagrange-multiplier saddle system remains selectable for numerical comparison. The first nominal pulse adjacent to each branch arm can additionally be subdivided by the “Junction local refine” factor, concentrating unknowns near the current discontinuity without refining the complete antenna. A feed located on an interior degree-2 node is represented as a delta gap distributed over the two adjacent pulse segments. Active input impedance and matching are then evaluated as Z_active = V_feed/I_feed and Γ = (Z_active−Z₀)/(Z_active+Z₀).</p>
<p class="note">For quantitative use, run the 3-level mesh convergence assistant and compare the reported active Z<sub>in</sub> between the current and finer meshes. The reduced basis enforces branch KCL exactly in its trial space, so a tiny KCL residual still does <b>not</b> by itself prove electromagnetic mesh convergence. Compare impedance and current distributions as the global mesh and local junction refinement are increased.</p>
<h3>Experimental linear rooftop current and line-charge reconstruction</h3>
<div class="eq">I_e(u) = (1−u) I_(e,0) + u I_(e,1), &nbsp; 0 ≤ u ≤ 1</div>
<div class="eq">I_open = 0, &nbsp; Σ_(e∈J) s_(J,e) I_(e,J) = 0</div>
<div class="eq">λ_e = −[1/(jω)] dI_e/ds = −[I_(e,1)−I_(e,0)]/(jω Δℓ_e)</div>
<div class="eq">Z_ab = Σ_(e∈supp a) Σ_(f∈supp b) ∬ ψ_a(s) t̂_e·G̿_E(r,r′)·t̂_f ψ_b(s′) ds′ds</div>
<p>The optional <b>Linear rooftop + charge</b> wire mode replaces a constant pulse on each mesh span with node-centred piecewise-linear trial/test functions. An open endpoint owns no current basis coefficient, so its current vanishes by construction. An ordinary degree-2 node contributes one rooftop function shared by its two incident spans. A degree-d T/Y/X node contributes d−1 independent signed modes whose outward endpoint currents satisfy KCL. The dense EFIE matrix is tested with the same linear functions. Far support pairs use four-point Gauss-Legendre integration on each span, while self/near pairs use radius-aware composite panels on both spans. The delta-gap voltage is projected weakly onto the basis functions meeting at the feed node.</p>
<p>The continuity equation then gives a directly reconstructed line-charge density λ on every span. The Results page plots |λ| against distance along the original CAD wire and reports maximum |λ|, open-end current and net continuity-charge diagnostics. The historical pulse mode also exposes an approximate finite-difference charge diagnostic so both representations can be inspected.</p>
<p class="note"><b>The rooftop/charge formulation remains experimental for closed wire loops even though its open-wire and hybrid convergence can be very good.</b> Since 5.41 the frozen 1.0 production basis is selected per validated use case: pulse/point-matching for the electrically small closed loop, and rooftop+RWG for the finite-ground monopole. The loop rooftop row remains visible as a non-blocking diagnostic because its localized nodal excitation produces about 31% normalized-pattern RMS against the uniform-current small-loop reference, whereas the pulse production row is about 8.35%. Critical designs still require mesh studies and independent full-wave/measurement comparison.</p>
<h3>Canonical physical validation bench</h3>
<p>The Antenna Designer now contains a non-destructive validation bench that solves canonical antennas in memory, independently of the geometry currently being edited. Every solver row is checked on a current and finer mesh, then compared with a separate physical reference when such a reference is meaningful.</p>
<div class="eq">ε_Z = |Z_fine − Z_current| / max(|Z_fine|, ε)</div>
<div class="eq">F_dipole(θ) = | cos[(π/2) cos θ] / sin θ |</div>
<div class="eq">R_r,small-loop ≈ 31200 (A/λ²)² Ω, &nbsp; F_loop(θ) = |sin θ|</div>
<p>The built-in cases are a thin centre-fed half-wave dipole, an electrically small circular loop, a quarter-wave monopole over a finite RWG ground plane and a rectangular-patch engineering benchmark. The dipole uses the familiar thin-wire 0.5λ reference near 73+j42.5 Ω and 2.15 dBi. The monopole uses image-theory values near half the dipole input impedance and approximately 5.15 dBi as an <i>infinite-ground</i> reference, while deliberately solving a finite 1λ square ground so finite-plane error remains visible. The patch case holds the Hammerstad/cavity seed geometry fixed and checks whether the hybrid minimum-|X| frequency remains near the intended f0.</p>
<p class="note"><b>PASS does not mean certified accuracy.</b> It combines mesh stability and broad canonical tolerances. Conversely, FAIL is useful: it exposes a physically important discrepancy even when the linear-system residual or KCL constraint is excellent. Since 5.22, the patch case deliberately compares the legacy explicit rooftop wire probe with an ideal differential PEC surface lumped port. Removing the probe self reactance makes the remaining patch surface/dielectric error easier to isolate; the benchmark remains FAIL when mesh stability or resonance placement is not adequate. The existing FDTD workspace is 2D TMz/TEz and is intentionally not used as a numerical truth reference for these 3D antennas; comparing them directly would test different Maxwell problems. A future 3D FDTD/FEM/NEC import can provide a genuinely independent full-wave column.</p>
<h3>PEC surface EFIE with RWG basis functions</h3>
<div class="eq">J_s(r) = Σ_n I_n f_n(r)</div>
<div class="eq">f_n^+(r) = l_n [r-r_n^+] /(2A_n^+), &nbsp; f_n^-(r) = l_n [r_n^- - r] /(2A_n^-)</div>
<div class="eq">∇_s·f_n^± = ± l_n/A_n^±</div>
<div class="eq">Z_mn = jωμ ∬ f_m(r)·f_n(r′) G(R) dS′dS + [1/(jωε)] ∬ (∇·f_m)(∇′·f_n) G(R) dS′dS</div>
<div class="eq">V_m = ∫ f_m(r)·E_inc(r) dS, &nbsp; [Z][I]=[V]</div>
<p>The standalone 4.1 surface solver assigns one Rao-Wilton-Glisson basis function to every interior edge shared by two triangles. The coefficient reconstructs a continuous normal surface current across that edge. In the hybrid solver, 4.5 additionally permits a <b>terminal-only half-RWG</b> on a boundary edge explicitly requested by a feed or galvanic junction; unrelated open edges remain excluded.</p>
<div class="eq">E_inc(r)=E₀ p̂ e^(−jk k̂·r), &nbsp; p̂·k̂=0</div>
<div class="eq">σ_RCS(r̂)= (ωμ)²/(4π|E₀|²) |(I̿−r̂r̂)·∫_S J_s(r′)e^(+jk r̂·r′)dS′|²</div>
<div class="eq">V_port,m ≈ V_gap l_m δ_(m,p), &nbsp; I_port ≈ I_p l_p, &nbsp; Z_in ≈ V_gap/I_port</div>
<p>The optional driven-surface mode maps a geometry feed to the nearest interior RWG edge and uses this first-order edge-gap excitation to estimate surface-antenna input impedance and radiation.</p>
<p><b>RWG singular/self-near quadrature (5.23):</b> same-triangle source integrals are now evaluated with a Duffy transformation. For a fixed observation point the triangle is split into three sub-triangles meeting at that point and the source mapping contributes a Jacobian proportional to u, which cancels the weak 1/R singularity analytically. Triangle pairs sharing an edge or vertex use a local one-level 1→4 composite quadrature on both triangles, while far pairs retain the compact symmetric rule. The historical equivalent-radius self factor remains loadable only for project compatibility and no longer enters the RWG matrix.</p>
<p><b>Differential RWG port normalization / footprint (5.24):</b> a two-surface lumped port is represented by scalar terminal potentials +V/2 and −V/2. If c+ and c− are the weighted integrals of div(f<sub>n</sub>) over the positive and negative terminal footprints, the circuit incidence vector is b=(c+−c−)/2, the MoM right-hand side is −Vb, and the power-dual source current is I=−b<sup>T</sup>i. The former full-difference excitation/current convention introduced a factor-of-four error in extracted Z<sub>in</sub>. The terminal weighting is now a smooth Gaussian integrated on the RWG mesh; keeping its physical σ fixed during refinement prevents the port area from shrinking with the local triangle size.</p>
<p class="note">This removes an arbitrary numerical self-radius from the surface EFIE, but it does not by itself make the printed-antenna model production-grade. Edge/vertex singular quadrature is still approximate. For convergence studies the differential patch port should use a fixed physical Gaussian σ so that its footprint does not shrink with the RWG mesh. Since 5.25 a finite-slab quasi-static dielectric image/fringing correction is available. Version 5.26 additionally provides a k<sub>rho</sub>-dependent Sommerfeld TM scalar-potential correction evaluated by inverse Hankel transform. The 5.26 increment is deliberately reactively projected until the matching TE/TM vector-potential dyadic is implemented, so it is a transition toward a layered MPIE rather than a complete one. Mesh convergence remains mandatory.</p>
<h3>Hybrid thin-wire + PEC-surface block MoM</h3>
<div class="eq">[ Z_ww &nbsp; Z_ws ; Z_sw &nbsp; Z_ss ] [ I_w ; I_s ] = [ E_gap ; 0 ]</div>
<div class="eq">E_t,wire = E_ww(I_w) + E_ws(I_s), &nbsp; ∫f_m·E_PEC dS = Z_sw I_w + Z_ss I_s = 0</div>
<div class="eq">F_total(r̂) = ∫_wire I(l)t̂ e^(+jk r̂·r) dl + ∫_S J_s(r)e^(+jk r̂·r) dS</div>
<p>The hybrid solver assembles thin-wire unknowns and RWG surface unknowns in one dense system. In pulse mode the wire block keeps the historical point-matching formulation. In rooftop mode, both wire trial and test functions are integrated and the two mutual blocks are assembled as true Galerkin integrals: <i>rooftop test ← RWG source</i> and <i>RWG test ← rooftop source</i>. A driven wire therefore induces PEC surface current and that surface current simultaneously changes the wire current and feed impedance. The radiated field is obtained from the coherent sum of the wire and surface current moments.</p>
<div class="eq">Z<sub>hyb</sub> = [ Z<sub>rr</sub> &nbsp; Z<sub>r,RWG</sub> ; Z<sub>RWG,r</sub> &nbsp; Z<sub>RWG</sub> ]</div>
<p>For a wire endpoint galvanically connected to PEC, the endpoint is no longer a free open end. The hybrid rooftop basis adds one <b>terminal half-rooftop / leakage mode</b>, allowing non-zero wire current at the contact. Since 5.21, current is balanced against the <b>integrated RWG divergence of a local terminal patch</b> rather than a single arbitrary edge. For a wire component with exactly one locally planar PEC terminal, the sub-cell return-current singularity unresolved by the surface mesh is extracted with a local PEC image Green contribution blended over the local RWG cell scale. Components connected between two or more PEC sheets deliberately skip this half-space approximation. At an internal degree-d T/Y/X wire node not connected to PEC, d−1 KCL-safe rooftop modes remain sufficient.</p>
<p><b>Reciprocity diagnostic (5.21):</b> rooftop→RWG and RWG→rooftop blocks are still integrated independently and their raw mismatch is reported. The matrix actually solved uses the Lorentz-reciprocal average Z<sub>ws</sub>=(Z<sub>ws</sub>+Z<sub>sw</sub><sup>T</sup>)/2. A large pre-symmetry mismatch therefore remains a warning about quadrature/near-contact resolution even though the solved mutual block is reciprocal by construction.</p>
<div class="eq">V_port = -V_0 b, &nbsp; b_n = ∫_{S+} ∇_s·f_n dS − ∫_{S−} ∇_s·f_n dS, &nbsp; I_port = -b^T I_RWG</div>
<p><b>Differential PEC surface lumped port (5.22):</b> for printed structures the hybrid workspace can solve the PEC sheets without an explicit thin-wire probe. A positive and a negative terminal patch are mapped to localized RWG-divergence stencils, the applied voltage is projected through the same dual vector used to extract source current, and the resulting Zin is therefore a surface-port quantity. This removes the free-space self reactance of a very short probe from the patch benchmark. It is an ideal lumped voltage discontinuity, not an exact coax aperture, via inductance or layered-medium port solution.</p>
<p>The solver reports a mutual-block reciprocity indicator based on the difference between Z<sub>r,RWG</sub> and Z<sub>RWG,r</sub><sup>T</sup> before row normalization. It should decrease with adequate quadrature/mesh refinement, but near-interaction regularization can prevent it from reaching machine precision.</p>
<p class="note">Pulse point-matched rows and Galerkin RWG rows have different units, while rooftop mode uses Galerkin wire rows. In both cases each algebraic row is normalized before elimination. Very-near wire/surface mutual terms still use equivalent-radius regularization.</p>
<h3>Wire ↔ PEC junctions and effective dielectric regions</h3>
<div class="eq">[ Z_ww &nbsp; Z_ws &nbsp; C_wᵀ ; Z_sw &nbsp; Z_ss &nbsp; C_sᵀ ; C_w &nbsp; C_s &nbsp; 0 ] [ I_w ; I_s ; λ_c ] = [ V ; 0 ; 0 ]</div>
<div class="eq">I_wire,node − I_RWG,edge = 0</div>
<div class="eq">η_pair = η_fill (L_overlap/L_pair), &nbsp; ε_r,eff = 1 + η_pair(ε_r−1), &nbsp; ε* = ε₀ ε_r,eff (1−j tanδ_eff)</div>
<p>Surface-referenced endpoint ports and passive junction markers map to the nearest topological wire node and to a nearby RWG edge. The Lagrange row enforces the local current-continuity constraint directly in the dense hybrid system. The two historical dielectric kernels remain effective-medium models: the recommended overlap form estimates the fraction of each source-observer segment that crosses a finite slab instead of switching the whole interaction from air to dielectric only from its midpoint.</p>
<h3>Finite layered-slab image / fringing correction (5.25)</h3>
<p>For an RWG surface-charge interaction on the two faces of a finite dielectric slab, the quasi-static spectral Dirichlet-to-Neumann relation can be written</p>
<div class="eq">[σ_t ; σ_b] = k_ρ [ ε₀+ε₂ coth(k_ρh) &nbsp; −ε₂ csch(k_ρh) ; −ε₂ csch(k_ρh) &nbsp; ε₀+ε₂ coth(k_ρh) ] [φ_t ; φ_b]</div>
<div class="eq">r_ε = (ε_r*−1)/(ε_r*+1), &nbsp; ε_r* = ε_r(1−j tanδ)</div>
<div class="eq">G_same,image ∝ [2/(ε₀(1+ε_r*))] Σ_(n≥1) (1+r_ε) r_ε^(2n−1) / √(ρ²+(2nh)²)</div>
<div class="eq">G_cross,slab ∝ [2/(ε₀(1+ε_r*))] Σ_(n≥0) (1+r_ε) r_ε^(2n) / √(ρ²+((2n+1)h)²)</div>
<p>When <b>Layered slab image / fringing (quasi-static)</b> is selected, the solver keeps the stable frequency-domain overlap/effective-medium Green kernel as its dynamic baseline and adds only a residual finite-slab quasi-static correction to the RWG scalar-potential block. The correction is weighted by the dielectric loading not already represented by the field-fill factor, so the Hammerstad seed is not counted twice. The same-face direct 1/R singular coefficient remains in the 5.23 Duffy/effective-medium term; only finite-distance images are added.</p>
<p class="note"><b>No PEC image plane is added.</b> The patch and ground plane are explicit RWG sheets in the MoM system, so adding a separate PEC image would double-count that boundary condition. This 5.25 model captures a controlled quasi-static dielectric-interface/fringing correction; it does not include the full Sommerfeld TE/TM spectral Green tensor, surface-wave poles or a volume-integral dielectric solution.</p>
<h3>Dynamic Sommerfeld TM scalar correction (5.26)</h3>
<div class="eq">gamma_i(k_rho) = sqrt(k_rho^2 - k_i^2), &nbsp; Re(gamma_i) >= 0</div>
<div class="eq">r_phi^TM = [gamma_2 - eps_r* gamma_1] / [gamma_2 + eps_r* gamma_1]</div>
<div class="eq">R_phi^TM = r(1-e^(-2 gamma_2 h)) / [1-r^2 e^(-2 gamma_2 h)]</div>
<div class="eq">T_phi^TM = (1-r^2)e^(-gamma_2 h) / [1-r^2 e^(-2 gamma_2 h)]</div>
<div class="eq">Delta G_phi(rho) = [1/(4 pi eps_0)] integral_0^infinity J_0(k_rho rho) [k_rho/gamma_1] Delta G_tilde_phi(k_rho) dk_rho</div>
<p>Version 5.26 evaluates the finite-slab TM scalar spectrum as a function of transverse wavenumber k<sub>rho</sub> and numerically inverse-transforms it with a Bessel/Hankel Sommerfeld integral. Propagating and evanescent spectral regions are therefore distinguished dynamically instead of being collapsed to the electrostatic reflection coefficient used by the 5.25 image series. The implementation uses a small limiting-loss term for branch/pole regularization, 16-point Gauss-Legendre panel integration and a cached 80-sample radial table so dense RWG assembly remains usable interactively.</p>
<p>For same-face interactions, the high-k<sub>rho</sub> limit r<sub>infinity</sub>=(1-eps_r*)/(1+eps_r*) is subtracted before the inverse transform. This keeps the direct singular coefficient assigned to the audited 5.23 Duffy treatment rather than silently introducing a second zero-distance interface singularity.</p>
<p class="note"><b>5.26 is not yet the complete layered-media MPIE.</b> A full mixed-potential formulation requires the consistent TE/TM vector-potential dyadic in addition to the scalar-potential Green function. Injecting the complete complex scalar dynamic increment by itself was found to violate passivity in a high-frequency two-plate sanity case. The production 5.26 path therefore retains the complete complex 5.25 baseline and adds only Re(G<sub>Sommerfeld</sub>-G<sub>QS</sub>) to the scalar Green function; because this scalar block is multiplied by 1/(j omega), that increment modifies the reactive impedance without pretending to add the missing radiative/loss balance.</p>
<h3>Passivity-guarded HED TE vector-potential transition (5.29)</h3>
<div class="eq">r_TE = (gamma_1-gamma_2)/(gamma_1+gamma_2), &nbsp; R_TE = r_TE(1-e^(-2 gamma_2 h)) / [1-r_TE^2 e^(-2 gamma_2 h)]</div>
<div class="eq">G_A,t^ref(rho) = [1/(4 pi)] integral_0^infinity J_0(k_rho rho) [k_rho/gamma_1] R_TE(k_rho) dk_rho</div>
<div class="eq">R_q = (k_z1^2/k_rho^2)(R_TE+R_TM), &nbsp; G_phi,HED ~ R_TE+R_q</div>
<p>Version 5.29 introduces the first paired vector-potential contribution for horizontal-electric-dipole-like RWG currents on a slab face. Tangential same-face RWG interactions receive a magnetic-vector-potential correction governed by the finite-slab TE reflection spectrum. In the HED mixed-potential gauge the associated scalar-potential spectrum couples TE and TM through R<sub>TE</sub>+R<sub>q</sub>; QTsignalApp evaluates that coupled spectrum as a diagnostic so the remaining MPIE pieces can be checked against the same spectral convention.</p>
<p>The same-face high-k<sub>rho</sub> asymptote remains separated from the inverse transform so the direct weak singularity stays assigned to the Duffy quadrature rather than being counted twice. The explicit PEC ground plane also remains an RWG unknown: no PEC image plane is introduced.</p>
<p class="note"><b>The 5.29 production matrix is deliberately passivity-guarded.</b> A trial that injected the complete complex HED scalar correction before the transmitted/longitudinal potential terms were available produced a negative input resistance in the otherwise passive 300 MHz two-plate sanity case. That formulation was rejected. The released 5.29 mode therefore adds the same-face TE vector-potential correction while retaining the audited 5.26 reactive scalar projection. Cross-face transmitted vector-potential normalization, arbitrary off-interface wire/surface dyadics, longitudinal mixed-potential completion and the layered far-field Green tensor are still outside this stage.</p>
<h3>Transmitted TE/TM tangential dyadic + reciprocity guard (5.30)</h3>
<div class="eq">G_A,t^trans = G_L ρhatρhat + G_T thatthat</div>
<div class="eq">G_L ∝ ∫ [T_TM(J_0−J_2)+T_TE(J_0+J_2)] (k_ρ/γ_1) dk_ρ</div>
<div class="eq">G_T ∝ ∫ [T_TM(J_0+J_2)+T_TE(J_0−J_2)] (k_ρ/γ_1) dk_ρ</div>
<p>Version 5.30 activates the opposite-face tangential vector-potential dyadic already evaluated by the 5.29 spectral table. The RWG test/source vectors are projected onto the in-plane separation direction ρhat and its orthogonal tangent that=n×ρhat. Because the ordinary effective-medium vector Green function is already present in the MoM baseline, the transmitted layered term is first converted to a residual ΔG<sub>A</sub>=G<sub>A,layered</sub>−G<sub>A,baseline</sub> so the direct interaction is not counted twice.</p>
<p>A full complex transmitted-vector trial was tested and rejected: at 300 MHz the passive two-plate case developed a small negative input resistance. The released path therefore keeps only Re(ΔG<sub>A</sub>) for opposite-face transmission. Multiplication by jωμ in the EFIE makes this a reactive correction, so 5.30 adds dynamic transmitted dispersion without pretending that the incomplete subset can carry the correct dissipative/radiative balance.</p>
<div class="eq">Z_RWG ← (Z_RWG + Z_RWG^T)/2</div>
<p>For the dedicated 5.30 mode, the assembled RWG block is also projected onto its reciprocal Galerkin average before row scaling. The raw pre-symmetry mismatch is reported separately. This is useful because Duffy/self and local composite near quadratures are source/observation oriented numerically even though the continuous reciprocal operator is symmetric.</p>
<p class="note"><b>5.30 is still not a general layered-media MPIE.</b> Same-face far-pair TE completion remains guarded because a naive all-pair extension degraded the patch mesh-convergence test. Arbitrary wire↔surface off-interface dyadics, longitudinal mixed-potential completion, complex transmitted power balance, surface-wave pole extraction and the layered far-field tensor remain future stages.</p>
<h3>Coupled HED longitudinal scalar reflection / transmission (5.31)</h3>
<div class="eq">K_Φ,HED ∝ S_0{(V_i^TE - V_i^TM)/k_ρ²}</div>
<div class="eq">H_ref = R_TE - (γ_1²/k_ρ²)(R_TE - R_φ^TM)</div>
<div class="eq">H_trans = T_TE - (γ_1²/k_ρ²)(T_TE - T_φ^TM)</div>
<p>Version 5.31 activates the coupled horizontal-electric-dipole scalar-potential spectrum that 5.29 had only evaluated diagnostically. The same-face scalar reflection and opposite-face scalar transmission therefore use the same TE/TM longitudinal mixed-potential gauge as the tangential HED vector-potential construction instead of the earlier TM-only transition.</p>
<p>For an air/dielectric/air slab, the cross-face expression has a useful homogeneous check: when ε<sub>r</sub>→1, T<sub>TE</sub>=T<sub>TM</sub>, the TE/TM difference term vanishes and H<sub>trans</sub> reduces to the ordinary direct homogeneous scalar spectrum. The same-face high-k<sub>ρ</sub> constant remains removed so the Duffy singular term is not counted twice.</p>
<p class="note"><b>The 5.31 scalar activation remains passivity-guarded.</b> Only the real Green-function increment relative to the audited quasi-static baseline is inserted. Because the scalar EFIE prefactor is 1/(jω), this is a reactive impedance correction. The full complex HED scalar term is intentionally still disabled until arbitrary off-interface source/observer dyadics and the complete layered power/radiation balance are available.</p>
<h3>Exterior-height Sommerfeld wire/RWG transition (5.32)</h3>
<div class="eq">P_z(k_ρ;d_o,d_s) = exp[-γ_1(d_o+d_s)]</div>
<div class="eq">G_A,ext^ref/trans(ρ,d_o,d_s) = H_0^{-1}{ P_z · R/T_TE,TM }</div>
<p>Version 5.32 extends the face-only spectral tables into the two exterior air half-spaces. For a source and observation point at distances d<sub>s</sub> and d<sub>o</sub> from their respective slab faces, every reflected/transmitted TE/TM spectral component acquires the vertical propagation factor exp[-γ<sub>1</sub>(d<sub>o</sub>+d<sub>s</sub>)]. At d<sub>o</sub>=d<sub>s</sub>=0 the exterior formulation reduces to the audited slab-face kernels.</p>
<p>The current production use is deliberately narrow: exterior wire↔RWG mutual vector-potential interactions receive the residual tangential TE/TM correction. Same-half-space pairs use the reflected spectrum; opposite-half-space pairs use the transmitted spectrum after subtracting the effective-medium direct vector Green term already present in the hybrid operator. The mutual wire/surface blocks are subsequently reciprocal-averaged as before.</p>
<p class="note"><b>5.32 still guards incomplete arbitrary-height physics.</b> Only Re(ΔG<sub>A,ext</sub>) is injected, so multiplication by jωμ produces a reactive correction. The corresponding exterior HED scalar spectrum is evaluated with the same height propagation but remains diagnostic in the wire↔surface path. Source or observation points located inside the dielectric volume do not use this exterior kernel and stay on the audited effective-medium baseline until internal-layer dyadics are completed.</p>
<h3>Internal-layer cavity and scalar-gradient transition (5.33)</h3>
<div class="eq">G_p^(2)(z,z_s) = [e^{-γ₂|z-z_s|} + r_p e^{-γ₂(z+z_s)} + r_p e^{-γ₂(2h-z-z_s)} + r_p² e^{-γ₂(2h-|z-z_s|)}]/[1-r_p²e^{-2γ₂h}]</div>
<div class="eq">ΔG_p^(2) = G_p^(2) - e^{-γ₂|z-z_s|}</div>
<div class="eq">E_Φ,corr = -(1/jω) ∇_t ΔG_Φ,HED · (∇·J)</div>
<p>Version 5.33 extends the guarded transition to a tangential current source or observation point located <b>inside medium 2</b>. The homogeneous dielectric direct term is already present in the effective-medium operator, so only the finite-slab multiple-reflection cavity residual ΔG is added. The internal TE/TM reflection coefficient is the interface reflection seen from medium 2, and the denominator sums repeated round trips between the two air/dielectric interfaces.</p>
<p>The same internal cavity spectrum is used to construct the coupled HED scalar residual. Its <b>tangential spatial gradient</b> is now activated in the rooftop-wire↔RWG mutual block. This closes one important gap left by 5.32: external and embedded tangential wire charge can now perturb the surface test equation through both the vector potential and the scalar-potential gradient. The two independently integrated wire↔surface blocks are still Lorentz-reciprocal averaged before solution.</p>
<p class="note"><b>5.33 remains deliberately guarded.</b> The internal vector and scalar Green increments are real-projected before the EFIE prefactors are applied, so the contribution remains reactive. Version 5.34 adds the first guarded normal-current extension described below; the full complex layered power balance and layered far field remain future stages.</p>
<h3>VED / via normal-current transition (5.34)</h3>
<p>A vertical electric current inside medium 2 excites the TM family. The 5.34 transition therefore reuses the medium-2 multiple-reflection TM cavity factor for two new residual quantities: a normal vector-potential component and a VED scalar-potential Green function. The homogeneous dielectric direct term is already present in the effective-medium baseline and is subtracted before the residual is applied.</p>
<div class="eq">ΔG<sub>Φ,VED</sub>(ρ,z,z′) ∝ ∫₀∞ J₀(k<sub>ρ</sub>ρ) (k<sub>ρ</sub>/γ₂) ΔC<sub>TM</sub>(k<sub>ρ</sub>;z,z′) dk<sub>ρ</sub></div>
<div class="eq">E<sub>Φ,VED,t</sub> = −(1/jω) ∇<sub>t</sub>ΔG<sub>Φ,VED</sub> (∂I/∂s)</div>
<p>For the reciprocal surface-charge → vertical-wire path, the observation gradient of the existing HED scalar residual is extended to include its normal derivative when the wire observation point lies strictly inside the slab. The wire↔RWG mutual blocks are still reciprocal-averaged before solution.</p>
<p class="note"><b>5.34 is a transition, not a complete VED dyadic.</b> The diagonal normal vector residual and the VED/HED scalar-gradient paths are active for predominantly normal internal vias/probes. The off-diagonal TM vector components that couple normal and tangential potential components, together with all full-complex radiative increments, remain guarded. New residuals are real-projected before the EFIE prefactors, so this stage modifies stored/reactive energy without claiming a complete layered-medium power balance.</p>
<div class="eq">γ_c = √(jωμσ), &nbsp; Z_s = (γ_c/σ)coth(γ_c t), &nbsp; P_cond ≈ ½ Re(Z_s) ∫_S |J_s|² dS</div>
<p>For printed conductors an optional local surface-impedance boundary term accounts for finite conductivity and copper thickness while retaining the zero-thickness RWG geometry. The thick-conductor limit approaches the usual skin-effect surface impedance.</p>
<p class="note">Midpoint-fill and slab-overlap remain engineering effective-medium approximations. The 5.25 layered option adds the finite-slab quasi-static image series described above. The 5.26 option adds dynamic k<sub>rho</sub>-dependent TM scalar dispersion through a Sommerfeld/Hankel transform with a reactive projection. The 5.29 option adds the same-face HED TE vector-potential correction and evaluates the coupled TE/TM HED scalar spectrum diagnostically. The 5.30 option adds a residual, reactively projected transmitted TE/TM tangential dyadic between opposite slab faces and reciprocal RWG symmetrization. The 5.31 option activates the coupled HED TE/TM longitudinal scalar reflection/transmission spectrum on slab-face RWG interactions under the same reactive guard. The 5.32 option propagates those spectra into the exterior air half-spaces. The 5.33 option adds the internal-medium cavity residual for tangential wire↔RWG coupling and activates the corresponding tangential HED scalar-gradient mutual term. The 5.34 option adds the guarded TM VED normal-current/vector and scalar-gradient transition for internal vias/probes. The 5.35 option activates the residual mixed TM G<sub>&rho;z</sub>/G<sub>z&rho;</sub> vector components under the same reactive and reciprocity guards. The 5.36 option then attempts a full-complex wire↔RWG HED/VED residual under an automatic passive-port fallback. The 5.37 option adds a propagating TE/TM reflected/transmitted far-field audit with separate upper/lower powers and a one-sided radiation-power guard. The 5.38 option additionally extracts/refines complex TE/TM guided-pole candidates of the same finite-slab spectrum; their power remains guarded until the analytic modal model uses the same explicit PEC-ground boundary as the RWG system. These remain controlled transitions toward a layered MPIE, not yet a rigorous general layered-media or volume-integral dielectric solution. Terminal half-RWG functions are deliberately localized to requested ports/junctions rather than forming a general open-surface boundary basis.</p>

<h3>VED mixed rho-z / z-rho vector transition (5.35)</h3>
<p>Version 5.35 completes the first guarded mixed-vector extension for a vertical electric dipole inside the dielectric layer. The TM cavity residual is differentiated with respect to observation/source height before the radial inverse transform. After separating the spectral 1/&gamma;<sub>2</sub> factor, the mixed projector contributes k<sub>&rho;</sub><sup>2</sup>/k<sub>2</sub><sup>2</sup> and the azimuthal inversion produces J<sub>1</sub>(k<sub>&rho;</sub>&rho;). The resulting residual components map a normal source current to a radial tangential response G<sub>&rho;z</sub>, and a radial tangential source to the reciprocal normal response G<sub>z&rho;</sub>.</p>
<p>The signed cavity factors used by the transform are proportional to -&gamma;<sub>2</sub><sup>-1</sup>&part;<sub>z</sub>C<sub>TM</sub> for observation and +&gamma;<sub>2</sub><sup>-1</sup>&part;<sub>z'</sub>C<sub>TM</sub> for the reciprocal source factor. This preserves the up/down propagation sign of every reflected path instead of treating the mixed term as an unsigned scalar image correction.</p>
<p class="note"><b>5.35 remains passivity guarded.</b> The mixed residuals are real-projected before the j&omega;&mu; vector-potential prefactor and the independently integrated wire&rarr;RWG / RWG&rarr;wire blocks are still replaced by their Lorentz-reciprocal average before solution. Thus the new terms improve via&harr;patch coupling without claiming the missing full-complex layered radiative power balance.</p>
<h3>Monitored complex wire/RWG transition (5.36)</h3>
<p>Version 5.36 is the first stage that can retain the complex Sommerfeld residual in the wire&rarr;RWG and RWG&rarr;wire HED/VED mutual operator. The vector residuals and the HED/VED scalar-gradient residuals are no longer forced to Re(G) when the 5.36 kernel is selected. This restores the candidate dissipative/radiative part of those mutual terms without changing the surface-only RWG operator, which remains on the previously audited guarded formulation.</p>
<p>Because an incomplete complex MPIE can violate passivity, the candidate solution is monitored at the driven ports. A candidate is accepted only if the solve is finite, accepted real power is non-negative, modeled conductor loss does not exceed accepted power, and every driven input impedance has non-negative real part within numerical tolerance. If any check fails, the program automatically re-solves with the 5.35 reactively projected VED off-diagonal model and reports the rejected candidate impedance/power for diagnosis.</p>
<div class="eq">P<sub>closure,preview</sub> = P<sub>accepted</sub> - P<sub>rad,free-space</sub> - P<sub>cond</sub></div>
<p class="note"><b>The 5.36 power closure is diagnostic, not yet a layered conservation proof.</b> The current far-field integral still uses the free-space coherent wire+RWG moment. It does not yet include the reflected/transmitted layered far-field tensor, surface-wave poles or dielectric absorption extracted from the Sommerfeld spectrum. A large closure residual therefore identifies missing power channels rather than automatically invalidating a passive 5.36 candidate.</p>
<h3>Propagating layered far-field power audit (5.37)</h3>
<p>Version 5.37 evaluates the propagating part of the finite-slab spectrum at the real exterior transverse wavenumber k<sub>rho</sub>=k<sub>0</sub>sin(theta). The coherent free-space current moment is decomposed into local TE and TM polarizations relative to the slab normal. On the equivalent source side the direct field is combined with the finite-slab reflection coefficient; in the opposite exterior half-space the finite-slab transmission coefficient is used.</p>
<div class="eq">F<sub>same</sub> = s-hat (1+R<sub>TE</sub>)F<sub>TE</sub> + p-hat (1+R<sub>TM</sub>)F<sub>TM</sub></div>
<div class="eq">F<sub>cross</sub> = s-hat T<sub>TE</sub>F<sub>TE</sub> + p-hat T<sub>TM</sub>F<sub>TM</sub></div>
<div class="eq">P<sub>prop</sub> = P<sub>upper</sub> + P<sub>lower</sub>, &nbsp; P<sub>unresolved</sub> = P<sub>accepted</sub> - P<sub>prop</sub> - P<sub>cond</sub></div>
<p>The outer media are both air, so the complete finite-slab transmission coefficient does not require an additional exterior wave-impedance ratio in the final power integral. The homogeneous limit epsilon<sub>r</sub>&rarr;1 gives R&rarr;0 and |T|&rarr;1, recovering the free-space propagating power.</p>
<p class="note"><b>5.37 is a propagating-spectrum audit, not the final Sommerfeld radiation operator.</b> The source distribution is represented by an equivalent driven-side current moment and only the real-angle propagating spectrum is integrated. Surface-wave pole residues, lateral-wave/branch-cut refinements, finite lateral substrate diffraction and explicit dielectric-loss power are still unresolved. The new guard is therefore one-sided: a complex candidate is rejected if P<sub>prop</sub>+P<sub>cond</sub> exceeds accepted power beyond tolerance, but a positive closure residual is retained as unresolved layered power rather than forced to zero.</p>
<h3>Guided-mode Sommerfeld pole audit (5.38)</h3>
<p>Version 5.38 searches the evanescent interval k<sub>0</sub>&lt;Re(k<sub>rho</sub>)&lt;|k<sub>2</sub>| for local minima of the same finite-slab TE/TM denominators used by the 5.37 reflection/transmission spectrum,</p>
<div class="eq">D<sub>p</sub>(k<sub>rho</sub>) = 1 - r<sub>p</sub><sup>2</sup> exp(-2&gamma;<sub>2</sub>h), &nbsp; p &isin; {TE,TM}</div>
<p>Each candidate is then Newton-refined in complex transverse wavenumber. The UI reports &beta;/k<sub>0</sub>, attenuation &alpha;=-Im(k<sub>rho</sub>) and the reflection-coefficient residue magnitude |N<sub>p</sub>/D'<sub>p</sub>|. The scan is quadratically clustered near the air light line so the fundamental modes of electrically thin PCB slabs are not lost between uniform samples.</p>
<p class="note"><b>P<sub>surface-wave</sub> is intentionally guarded in 5.38.</b> These poles belong to the analytic air/dielectric/air slab denominator, while a microstrip ground plane in QTsignalApp is still an explicit finite PEC RWG sheet. Converting the bare-slab residue into power would therefore use a modal boundary condition different from the one solved by MoM. Version 5.38 reports the poles as spectral diagnostics but keeps P<sub>surface-wave</sub>=0 in the closure until a grounded-stack dispersion relation, modal normalization and current-to-mode overlap integral use the same PEC boundary model.</p>
<h3>Grounded PEC modal reference (5.39)</h3>
<p>Version 5.39 adds a separate air/dielectric/PEC reference problem when an explicit RWG backing plane covers a meaningful fraction of the substrate negative-normal face. For a bound mode with &alpha;<sub>1</sub>=sqrt(&beta;<sup>2</sup>-k<sub>0</sub><sup>2</sup>) and q<sub>2</sub>=sqrt(k<sub>2</sub><sup>2</sup>-&beta;<sup>2</sup>), the grounded-slab equations are</p>
<div class="eq">TM: q<sub>2</sub> tan(q<sub>2</sub>h) - &epsilon;<sub>r</sub>&alpha;<sub>1</sub> = 0</div>
<div class="eq">TE: q<sub>2</sub> cot(q<sub>2</sub>h) + &alpha;<sub>1</sub> = 0</div>
<p>The roots are complex-refined and the UI reports &beta;/k<sub>0</sub>, attenuation, an estimate of backing-plane coverage and a forward modal power normalization in W/m. TM normalization uses unit tangential H at the dielectric/air interface; TE uses unit tangential E.</p>
<p class="note"><b>5.39 still does not manufacture P<sub>surface-wave</sub> from the closure residual.</b> The infinite grounded-slab mode now uses the correct PEC boundary type, but exact excitation by the finite RWG ground/patch current distribution still requires a validated pole-overlap operator. That exact finite-ground power is explicitly post-1.0 unless a frozen validation benchmark proves it necessary.</p>
<h3>Hybrid broadband sweep (5.40)</h3>
<div class="eq">[Z<sub>hyb</sub>(fᵢ)] [I(fᵢ)] = [V<sub>port</sub>(fᵢ)]</div>
<p>Version 5.40 removes the thin-wire-only broadband blocker. The sweep now reuses the exact Hybrid port configuration, PEC triangulation budget, finite-conductivity settings, dielectric regions and selected Sommerfeld kernel used by the single-frequency solve. A fixed surface triangulation is reused across the sampled band while the frequency-dependent wire discretization, Green kernels and coupled matrix are rebuilt at each point. S11, VSWR and the Smith locus are computed directly from each returned Hybrid feed result.</p>
<h3>Frozen 1.0 validation closure (5.41)</h3>
<p>Version 5.41 does not add a new solver family. It freezes one production path per canonical use case and keeps alternative/legacy rows visible as diagnostics. The electrically small closed loop uses pulse/point matching for the 1.0 gate; the finite-ground monopole uses rooftop+RWG because its impedance mesh delta is about 8.63% and its coarse/fine normalized-pattern RMS is about 0.45%.</p>
<p>For the printed patch, the broad Gaussian differential port retains a non-zero capacitive numerical reactance even when the dominant patch mode is already near resonance. Therefore the 1.0 modal-resonance gate is based on the fixed-voltage surface-current response <i>∫|J<sub>s</sub>|² dS</i>, with a three-point parabolic interpolation around the sampled maximum. The original min-|X| frequency remains displayed as a separate port-reactance diagnostic. On the canonical benchmark the modal estimate is about 1.08045 f<sub>0</sub> and the fine/coarse impedance delta is about 4.35%, satisfying the frozen ≤10% and ≤7.5% gates without changing the layered Green operator.</p>
<p class="note">The sweep does not interpolate a single-frequency impedance. It is a sequence of independent full coupled solves. The 5.36+ passivity fallback remains active point-by-point and the UI reports how many sampled frequencies required the guarded reactive fallback. Far-field integration stays disabled during matching sweeps for performance; run the selected single-frequency Hybrid solve at frequencies of interest for radiation results.</p>
<h3>Simulation / Antenna Designer 1.0 scope</h3>
<p>The 1.0 target is intentionally finite. The production layered-media scope is one isotropic dielectric slab with an optional explicit PEC ground, hybrid wire/RWG solving, hybrid broadband sweep, consistent S-parameter/radiation views, passive guards and frozen dipole/loop/monopole/patch validation gates. A general multilayer anisotropic MPIE, exact finite-ground surface-wave power, volumetric coax apertures, branch-cut/lateral-wave decomposition and GPU/FMM acceleration are post-1.0 research features unless required to close a frozen benchmark.</p>
<p class="note"><b>1.0 maintenance rule:</b> QTsignalApp 6.0.0 reached the frozen Simulation / Antenna Designer 1.0 milestone. The 6.0.x line accepts demonstrated regression fixes and reporting/diagnostic clarification; new physical model families remain post-1.0 work.</p>
<h3>Printed microstrip / inset-patch design start</h3>
<div class="eq">ε_eff ≈ (ε_r+1)/2 + (ε_r−1)/[2√(1+12h/W)] &nbsp; (+ narrow-line correction)</div>
<div class="eq">W_patch = c₀/(2f) √[2/(ε_r+1)]</div>
<div class="eq">ΔL/h = 0.412 [(ε_eff+0.3)(W/h+0.264)] / [(ε_eff−0.258)(W/h+0.8)]</div>
<div class="eq">L_patch = c₀/[2f√ε_eff] − 2ΔL</div>
<div class="eq">R_in(y) ≈ R_edge cos²(πy/L), &nbsp; y_inset = (L/π) acos√(R_target/R_edge)</div>
<p>The inset-patch surface is triangulated as one conformal PEC conductor: patch body, side ears and feed strip reuse matching contact-plane divisions so their shared interfaces become true interior RWG edges. The two notch gaps intentionally remain open boundaries.</p>
<div class="eq">Z₀,coax ≈ (60/√ε_r) ln(b/a), &nbsp; Z_ref = Z₀ [Z_A + Z₀ tanh(γl)] / [Z₀ + Z_A tanh(γl)]</div>
<p>The optional coax setting moves the reported impedance/S11 reference plane through an external TEM line. It does not change the solved antenna-terminal current or replace the thin-wire transition with a volumetric coax/aperture field model.</p>
<p class="note">Microstrip width, rectangular-patch dimensions and inset depth are first-order quasi-TEM/cavity estimates used to seed geometry. Since 5.22 the patch presets seed the effective-medium fill from the same Hammerstad ε<sub>eff</sub> used for the geometry and preselect the ideal differential PEC surface port; the old vertical probe remains available as geometry/legacy reference. Version 5.23 introduced Duffy RWG self/near quadrature and 5.24 fixed the differential-port normalization and physical Gaussian footprint. Version 5.25 adds a residual quasi-static finite-slab dielectric image/fringing correction while keeping the explicit RWG ground plane. Version 5.26 adds a dynamic Sommerfeld TM scalar correction with a passivity-preserving reactive projection. Version 5.29 additionally activates the same-face HED TE vector-potential correction and the TE/TM HED scalar diagnostic. Version 5.30 adds the residual transmitted TE/TM tangential dyadic across the substrate and reciprocal RWG symmetrization. Version 5.31 activates the coupled HED TE/TM longitudinal scalar reflection/transmission spectrum on slab faces. Version 5.32 adds the exterior-height propagation factor exp[-γ1(dobs+dsrc)] and uses the resulting TE/TM vector residual in wire↔RWG mutual coupling. Version 5.33 adds a multiple-reflection internal-layer TE/TM cavity residual and activates the real-projected tangential HED scalar-gradient correction for rooftop wire↔RWG coupling. Version 5.34 adds the first guarded TM VED/via transition: diagonal normal vector residual, VED scalar-gradient coupling and reciprocal HED normal-gradient response. Version 5.35 activates the residual mixed TM G<sub>&rho;z</sub>/G<sub>z&rho;</sub> components from signed cavity derivatives and a J<sub>1</sub> transform. Version 5.36 can retain the full complex wire↔RWG residual when the passive-port monitor succeeds, with automatic fallback otherwise. Version 5.37 adds the first propagating TE/TM slab far-field, separates upper/lower exterior radiation and adds a one-sided propagating-power guard. Version 5.38 locates and complex-refines the bare-slab TE/TM guided poles. Version 5.39 adds a grounded air/dielectric/PEC modal reference and W/m normalization while still guarding finite-ground excitation power. Version 5.40 replaces the antenna broadband blocker with a shared-input Hybrid frequency sweep, keeping thin-wire as an explicit legacy/fast option. Version 5.41 closes the frozen canonical validation gates without changing the layered operator: production-path selection is explicit, the patch gate uses the integrated modal-current response peak, and the Gaussian-port min-|X| remains a distinct diagnostic. Validate critical designs against an independent layered-medium MoM, FEM/FDTD or measurements.</p>
<h3>Broadband re-solve</h3>
<div class="eq">[Z(fᵢ)] [I(fᵢ)] = [E_gap(fᵢ)] &nbsp; for each sampled fᵢ</div>
<p>A broadband antenna sweep assembles and solves a new complex MoM system at every frequency because k=2πf/c, the Green kernel, current distribution and mutual coupling all vary with frequency. Far-field post-processing can be disabled during this repeated solve when only input matching is required.</p>
<h3>Geometry optimization loop</h3>
<div class="eq">geometry(p) → mesh(p) → Z(p) I(p) = E_gap → Z_in(p) → J(p)</div>
<p>The optimizer never scales an already-computed impedance. Every candidate is re-meshed and re-solved. For one-dimensional parasitic-spacing optimization each disconnected parasitic component is translated relative to the observed-feed component while its own conductor length is preserved.</p>
<h3>Multi-parameter Yagi-array objective</h3>
<div class="eq">p = [s_d, s_r, s_dirs, s_R, s_dirs,pos]ᵀ</div>
<p>The normalized variables scale the driven-element length, reflector length, all director lengths, reflector position and all director boom positions. For an array with several directors, their relative length taper and relative spacing are preserved while the director-length and director-position factors are optimized globally.</p>
<div class="eq">FBR = |E(φ_front)|² / |E(φ_front + π)|²</div>
<div class="eq">fᵢ = f₀[1 − δ + 2δ i/(N_f−1)]</div>
<div class="eq">J_band,mean = (1/N_f) Σᵢ |Γ(fᵢ)| &nbsp;&nbsp; ; &nbsp;&nbsp; J_band,worst = maxᵢ |Γ(fᵢ)|</div>
<div class="eq">J = [w_Γ |Γ(f₀)| + w_D / D + w_FB / FBR + w_BW J_band] / [w_Γ+w_D+w_FB+w_BW]</div>
<p>Here D is linear directivity and the front direction is the boom direction from the inferred reflector through the driven element toward the directors. The band term can use either the mean or the worst sampled reflection coefficient over the selected fractional band. It is more informative than a two-edge proxy, but the final bandwidth must still be established with a dense frequency sweep and a stated criterion such as VSWR ≤ 2.</p>
<div class="eq">Δs_adj/λ ≥ Δs_min &nbsp;&nbsp; ; &nbsp;&nbsp; L_boom/λ ≤ L_boom,max</div>
<p>Geometric constraints reject candidates before matrix assembly when adjacent element-center spacing is below the selected normalized minimum or the boom exceeds the selected normalized maximum. Setting the maximum boom length to zero disables that upper constraint.</p>
<p>The 2.8 optimizer uses bounded derivative-free coordinate search: each active parameter is sampled while the others are held at their current best values, then its interval is contracted around the best sample before the next pass. This remains a local numerical search rather than a proof of the global optimum.</p>
<h3>Element-wise Yagi refinement</h3>
<div class="eq">p_ind = [s_Ld, s_Lr, s_R, s_LD1, s_SD1, …, s_LDN, s_SDN]ᵀ</div>
<p>In the 3.0 refinement stage, each director Dᵢ can have an independent length factor s_LDi and boom-position factor s_SDi. A length factor scales the element about its own centroid; a position factor scales its centroid vector relative to the driven-element centroid. Each coordinate may be frozen or assigned its own bounded interval.</p>
<div class="eq">x_R &lt; x_D = 0 &lt; x_D1 &lt; x_D2 &lt; … &lt; x_DN</div>
<p>The ordering relation is evaluated along the inferred forward boom axis and is enforced together with the minimum-spacing and maximum-boom constraints. This prevents coordinate search from exchanging director identities or moving the reflector into the forward half-plane.</p>
<p>Because the parameter dimension grows as 3+2N, the individual-director stage is best used after a lower-dimensional grouped optimization. Every accepted coordinate trial still performs the same complete remesh and MoM re-solve; the method remains a local derivative-free engineering search.</p>
<p class="note">In the historical pulse baseline, constant pulse basis functions do not impose exact current continuity at every ordinary degree-2 mesh node, so QTsignalApp reports a normalized local current-discontinuity diagnostic; T/Y/X nodes use the reduced or legacy KCL treatment described above. The experimental rooftop mode instead embeds open-end, ordinary-node and branched-node current continuity in its trial space and reconstructs distributed line charge from dI/ds. Neither formulation should be interpreted as a fully validated NEC replacement. Finite conductivity and dielectric loading are not represented in the standalone wire-only solve, and the hybrid solver supports both pulse and rooftop wire bases. Verify segmentation/radius convergence and compare critical designs with NEC/FEM/full-wave software.</p>
<h2>Mutual inductance by the Neumann integral</h2>
<div class="eq">M = (μ / 4π) N₁N₂ ∮∮ (dℓ₁ · dℓ₂) / |r₁ − r₂|, &nbsp; μ=μ₀μᵣ</div>
<div class="eq">k = M / √(L₁L₂)</div>
<p>The geometric coupling tab discretizes two arbitrarily oriented circular loops in 3D and evaluates the double line integral numerically. With μᵣ=1 this is the air-core result.</p>
<p class="note">Using μᵣ&gt;1 scales the Neumann/self-inductance model as a homogeneous or effective magnetic medium. It does not solve a localized ferrite core: real core shape, air gaps, saturation and fringing require magnetostatic FEM/BEM or a dedicated magnetic-circuit model.</p>
)HTML"), palette()), QStringLiteral("Numerical methods"));

    if (m_tabs->count() > 0)
    {
        int restoreIndex = 0;
        if (!currentTabText.isEmpty())
        {
            for (int i = 0; i < m_tabs->count(); ++i)
            {
                if (m_tabs->tabText(i) == currentTabText)
                {
                    restoreIndex = i;
                    break;
                }
            }
        }
        m_tabs->setCurrentIndex(restoreIndex);
    }

}


void ElectromagnetismReferenceWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) return;
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
        rebuildPages();
}