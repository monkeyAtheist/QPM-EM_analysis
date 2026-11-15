#include "em_units_calculator_model.h"
#include "em_calculator_model.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace EmCalculator
{
namespace
{
using D = std::array<double, 7>;
constexpr D dimensionless{};
// kg, m, s, A, K, mol, cd
constexpr D kilogram = {1,0,0,0,0,0,0};
constexpr D metre    = {0,1,0,0,0,0,0};
constexpr D second   = {0,0,1,0,0,0,0};
constexpr D ampere   = {0,0,0,1,0,0,0};
constexpr D kelvin   = {0,0,0,0,1,0,0};
constexpr D mole     = {0,0,0,0,0,1,0};
constexpr D candela  = {0,0,0,0,0,0,1};
constexpr D hertz    = {0,0,-1,0,0,0,0};
constexpr D coulomb  = {0,0,1,1,0,0,0};
constexpr D newton   = {1,1,-2,0,0,0,0};
constexpr D joule    = {1,2,-2,0,0,0,0};
constexpr D watt     = {1,2,-3,0,0,0,0};
constexpr D volt     = {1,2,-3,-1,0,0,0};
constexpr D ohm      = {1,2,-3,-2,0,0,0};
constexpr D farad    = {-1,-2,4,2,0,0,0};
constexpr D henry    = {1,2,-2,-2,0,0,0};
constexpr D tesla    = {1,0,-2,-1,0,0,0};
constexpr D siemens  = {-1,-2,3,2,0,0,0};
constexpr D epsDim   = {-1,-3,4,2,0,0,0};
constexpr D muDim    = {1,1,-2,-2,0,0,0};
constexpr D kDim     = {1,3,-4,-2,0,0,0};
constexpr D fieldDim = {1,1,-3,-1,0,0,0};

Quantity quantity(double v, D d = dimensionless) { return {v,d}; }
D addD(D a, const D &b, double scale = 1.0)
{
    for (std::size_t i = 0; i < a.size(); ++i) a[i] += b[i] * scale;
    return a;
}
bool sameD(const D &a, const D &b)
{
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::fabs(a[i]-b[i]) > 1e-10) return false;
    return true;
}
bool finite(const Quantity &q)
{
    if (!std::isfinite(q.siValue)) return false;
    for (const double d : q.dimensions) if (!std::isfinite(d)) return false;
    return true;
}
std::string normalized(std::string s)
{
    // UTF-8 ASCII-friendly conveniences for pasted scientific notation.
    const std::pair<const char *, const char *> substitutions[] = {
        {"²", "^2"}, {"³", "^3"}, {"π", "pi"}, {"ε₀", "eps0"},
        {"ε0", "eps0"}, {"μ₀", "mu0"}, {"µ", "u"}, {"μ", "u"}, {"Ω", "Ohm"},
        {"−", "-"}, {"×", "*"}, {"÷", "/"}
    };
    for (const auto &item : substitutions)
    {
        std::size_t pos = 0;
        while ((pos = s.find(item.first, pos)) != std::string::npos)
        {
            s.replace(pos, std::string(item.first).size(), item.second);
            pos += std::string(item.second).size();
        }
    }
    return s;
}

const std::unordered_map<std::string, Quantity> &units()
{
    static const auto map = [] {
        std::unordered_map<std::string, Quantity> result;
        auto put = [&result](const char *symbol, double factor, D dim) {
            result.emplace(symbol, quantity(factor, dim));
        };
        put("kg", 1, kilogram); put("g",1e-3,kilogram); put("mg",1e-6,kilogram);
        put("m",1,metre); put("km",1e3,metre); put("cm",1e-2,metre);
        put("mm",1e-3,metre); put("um",1e-6,metre); put("nm",1e-9,metre);
        put("s",1,second); put("ms",1e-3,second); put("us",1e-6,second);
        put("ns",1e-9,second); put("min",60,second); put("hour",3600,second);
        put("A",1,ampere); put("mA",1e-3,ampere); put("uA",1e-6,ampere);
        put("nA",1e-9,ampere); put("K",1,kelvin); put("mol",1,mole);
        put("cd",1,candela); put("Hz",1,hertz); put("kHz",1e3,hertz);
        put("MHz",1e6,hertz); put("GHz",1e9,hertz);
        put("C",1,coulomb); put("mC",1e-3,coulomb); put("uC",1e-6,coulomb);
        put("nC",1e-9,coulomb); put("pC",1e-12,coulomb);
        put("N",1,newton); put("mN",1e-3,newton);
        put("J",1,joule); put("mJ",1e-3,joule); put("uJ",1e-6,joule);
        put("eV",evJoule,joule); put("keV",1e3*evJoule,joule); put("MeV",1e6*evJoule,joule);
        put("W",1,watt); put("mW",1e-3,watt); put("kW",1e3,watt);
        put("V",1,volt); put("mV",1e-3,volt); put("kV",1e3,volt);
        put("F",1,farad); put("mF",1e-3,farad); put("uF",1e-6,farad);
        put("nF",1e-9,farad); put("pF",1e-12,farad);
        put("H",1,henry); put("mH",1e-3,henry); put("uH",1e-6,henry);
        put("T",1,tesla); put("mT",1e-3,tesla); put("uT",1e-6,tesla);
        put("Ohm",1,ohm); put("ohm",1,ohm); put("kohm",1e3,ohm);
        put("kOhm",1e3,ohm); put("Mohm",1e6,ohm);
        put("S",1,siemens); put("mS",1e-3,siemens);
        put("rad",1,dimensionless); put("deg",pi/180.0,dimensionless);
        return result;
    }();
    return map;
}

const std::unordered_map<std::string, Quantity> &constants()
{
    static const auto map = [] {
        std::unordered_map<std::string, Quantity> result;
        const std::unordered_map<std::string,D> dimensions = {
            {"pi",dimensionless},{"e",dimensionless},{"eps0",epsDim},{"mu0",muDim},
            {"c0",addD(metre,second,-1)},{"ke",kDim},{"eta0",ohm},
            {"h",addD(joule,second)},{"hbar",addD(joule,second)},
            {"qe",coulomb},{"ev",joule},{"me",kilogram},{"mp",kilogram},
            {"kb",addD(joule,kelvin,-1)},{"na",addD(dimensionless,mole,-1)},
            {"rgas",addD(addD(joule,kelvin,-1),mole,-1)},
            {"faraday",addD(coulomb,mole,-1)},
            {"gconst",D{-1,3,-2,0,0,0,0}},
            {"sigmasb",D{1,0,-3,0,-4,0,0}},
            {"sigma_cu",addD(siemens,metre,-1)}
        };
        for (const auto &info : physicalConstants())
        {
            const auto d = dimensions.find(info.symbol);
            if (d == dimensions.end()) continue;
            const Quantity value = quantity(info.value,d->second);
            result.emplace(info.symbol, value);
            std::size_t start = 0;
            while (start < info.aliases.size())
            {
                const auto comma = info.aliases.find(',',start);
                auto alias = info.aliases.substr(start,comma == std::string::npos ? std::string::npos : comma-start);
                if (!alias.empty()) result.emplace(alias,value);
                if (comma == std::string::npos) break;
                start = comma+1;
            }
        }
        return result;
    }();
    return map;
}

class QuantityParser
{
public:
    QuantityParser(std::string input, const VariableTable &vars)
        : m_text(normalized(std::move(input))), m_vars(vars) {}

    QuantityResult run()
    {
        QuantityResult result;
        try
        {
            skip();
            if (m_text.size() > 8192) fail("Expression is too long (8192-character limit).");
            if (m_pos == m_text.size()) fail("Empty expression.");
            result.quantity = expression();
            skip();
            if (m_pos != m_text.size()) fail("Unexpected token or missing multiplication operator.");
            if (!finite(result.quantity)) fail("Non-finite value or dimension exponent.");
            result.ok = true;
        }
        catch (const std::runtime_error &e)
        {
            result.error = e.what();
            result.errorPosition = m_errorPos;
        }
        return result;
    }

private:
    Quantity expression()
    {
        if (++m_depth > 128) fail("Expression nesting is too deep.");
        Quantity lhs = term();
        for (;;)
        {
            skip();
            char operation = 0;
            if (accept('+')) operation = '+';
            else if (accept('-')) operation = '-';
            else break;
            const Quantity rhs = term();
            if (!sameD(lhs.dimensions, rhs.dimensions)) fail("Incompatible units for addition/subtraction.");
            lhs.siValue += (operation == '+' ? 1.0 : -1.0)*rhs.siValue;
            check(lhs);
        }
        --m_depth;
        return lhs;
    }

    Quantity term()
    {
        Quantity lhs = unary();
        for (;;)
        {
            skip();
            char operation = 0;
            if (accept('*')) operation='*';
            else if (accept('/')) operation='/';
            else break;
            const bool savedBareUnit=m_allowBareUnit;
            // Explicit multiplication/division always permits a unit token on its RHS.
            // Bare units without an operator are not accepted.
            m_allowBareUnit = true;
            const Quantity rhs=unary();
            m_allowBareUnit=savedBareUnit;
            if (operation == '*')
            {
                lhs.siValue *= rhs.siValue;
                lhs.dimensions = addD(lhs.dimensions,rhs.dimensions);
            }
            else
            {
                if (rhs.siValue == 0.0) fail("Division by zero.");
                lhs.siValue /= rhs.siValue;
                lhs.dimensions = addD(lhs.dimensions,rhs.dimensions,-1.0);
            }
            check(lhs);
        }
        return lhs;
    }

    Quantity unary()
    {
        skip();
        if (accept('+')) return unary();
        if (accept('-'))
        {
            Quantity v=unary(); v.siValue=-v.siValue; return v;
        }
        return power();
    }

    Quantity power()
    {
        Quantity lhs=primary();
        skip();
        if (accept('^')) lhs=raised(lhs,unary()); // right associative
        return lhs;
    }

    Quantity primary()
    {
        skip();
        if (accept('('))
        {
            Quantity value=expression();
            if (!accept(')')) fail("Missing closing ')'.");
            return value;
        }
        if (m_pos < m_text.size() && (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos]=='.'))
            return number();
        if (m_pos < m_text.size() && (std::isalpha(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos]=='_'))
        {
            std::string id=identifier();
            skip();
            if (accept('(')) return function(id);
            const auto variable = m_vars.find(id);
            if (variable != m_vars.end()) return variable->second;
            // Allow composite units such as 3 m/s, but do not interpret an
            // isolated `F` as one farad when a named force F was forgotten.
            if (m_allowBareUnit)
            {
                const auto unit = units().find(id);
                if (unit != units().end()) return unit->second;
            }
            if (id == "E") fail("Unknown variable E. Re-evaluate the worksheet; Euler's number is lowercase e.");
            if (id == "ans") fail("ans is not defined yet. Evaluate an expression first.");
            std::transform(id.begin(),id.end(),id.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
            const auto constant = constants().find(id);
            if (constant != constants().end()) return constant->second;
            fail("Unknown variable, physical constant or unit: " + id);
        }
        fail("Expected a number, variable, function or parenthesized expression.");
        return {};
    }

    Quantity number()
    {
        const char *start=m_text.c_str()+m_pos;
        char *end=nullptr;
        errno=0;
        const double number=std::strtod(start,&end);
        if (start == end || errno == ERANGE || !std::isfinite(number)) fail("Invalid numeric literal.");
        m_pos+=static_cast<std::size_t>(end-start);
        Quantity result=quantity(number);
        const std::size_t oldPos=m_pos;
        skip();
        if (m_pos < m_text.size() && std::isalpha(static_cast<unsigned char>(m_text[m_pos])))
        {
            const auto token=identifier();
            const auto unit=units().find(token);
            if (unit != units().end())
            {
                // Bind an exponent immediately following a unit suffix to the
                // unit, not to the preceding numeric coefficient:
                //   9 m^2 = 9 * (m^2), while (9 m)^2 = 81 * m^2.
                // Preserve right-associative exponentiation in the exponent.
                Quantity unitValue = unit->second;
                if (accept('^')) unitValue = raised(unitValue, unary());
                result.siValue *= unitValue.siValue;
                result.dimensions = unitValue.dimensions;
                check(result);
                return result;
            }
        }
        m_pos=oldPos;
        return result;
    }

    std::string identifier()
    {
        const auto begin=m_pos;
        while (m_pos < m_text.size())
        {
            const unsigned char ch=static_cast<unsigned char>(m_text[m_pos]);
            if (!(std::isalnum(ch) || ch=='_')) break;
            ++m_pos;
        }
        return m_text.substr(begin,m_pos-begin);
    }

    Quantity function(const std::string &id)
    {
        Quantity a=expression(), b;
        const bool two=accept(',');
        if (two) b=expression();
        if (!accept(')')) fail("Missing closing ')' in function.");
        const auto scalar=[this](const Quantity &q) {
            if (!sameD(q.dimensions,dimensionless)) fail("Function requires a dimensionless input.");
            return q.siValue;
        };
        const auto single=[this](bool condition) { if (!condition) fail("Incorrect number of function arguments."); };
        if (id=="pow") { single(two); return raised(a,b); }
        if (id=="min" || id=="max")
        {
            single(two);
            if (!sameD(a.dimensions,b.dimensions)) fail("min/max arguments must have compatible units.");
            return quantity(id=="min" ? std::min(a.siValue,b.siValue) : std::max(a.siValue,b.siValue),a.dimensions);
        }
        single(!two);
        if (id=="sqrt") return raised(a,quantity(0.5));
        if (id=="abs") return quantity(std::fabs(a.siValue),a.dimensions);
        const double x=scalar(a);
        double y=0.0;
        if (id=="sin") y=std::sin(x);
        else if (id=="cos") y=std::cos(x);
        else if (id=="tan") y=std::tan(x);
        else if (id=="sind") y=std::sin(x*pi/180.0);
        else if (id=="cosd") y=std::cos(x*pi/180.0);
        else if (id=="tand") y=std::tan(x*pi/180.0);
        else if (id=="asin") y=std::asin(x);
        else if (id=="acos") y=std::acos(x);
        else if (id=="atan") y=std::atan(x);
        else if (id=="exp") y=std::exp(x);
        else if (id=="ln" || id=="log") y=std::log(x);
        else if (id=="log10") y=std::log10(x);
        else if (id=="floor") y=std::floor(x);
        else if (id=="ceil") y=std::ceil(x);
        else if (id=="deg2rad") y=x*pi/180.0;
        else if (id=="rad2deg") y=x*180.0/pi;
        else fail("Unknown function: " + id);
        const auto output=quantity(y); check(output);
        return output;
    }

    Quantity raised(const Quantity &a,const Quantity &b)
    {
        if (!sameD(b.dimensions,dimensionless)) fail("Exponent must be dimensionless.");
        Quantity result=quantity(std::pow(a.siValue,b.siValue));
        for (std::size_t i=0; i<7; ++i) result.dimensions[i]=a.dimensions[i]*b.siValue;
        check(result);
        return result;
    }
    void check(const Quantity &q) { if (!finite(q)) fail("Numeric overflow or invalid domain (NaN/Inf)."); }
    bool accept(char c)
    {
        skip();
        if (m_pos < m_text.size() && m_text[m_pos]==c) {++m_pos;return true;}
        return false;
    }
    void skip() {while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;}
    [[noreturn]] void fail(const std::string &error)
    {
        m_errorPos=m_pos;
        throw std::runtime_error(error);
    }
    std::string m_text;
    const VariableTable &m_vars;
    std::size_t m_pos=0;
    std::size_t m_errorPos=0;
    bool m_allowBareUnit=false;
    int m_depth=0;
};

std::string trim(const std::string &s)
{
    const auto first=s.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
bool validVariable(const std::string &name)
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0]=='_')) return false;
    for (const unsigned char c: name) if (!(std::isalnum(c) || c=='_')) return false;
    // Prevent context-dependent readings such as `m=2`, `3*m` (scalar),
    // but `3 m` (length). Unit symbols are reserved, case sensitively.
    if (units().find(name)!=units().end() || name=="ans") return false;
    // Constant aliases are case-insensitive on lookup when no variable exists;
    // block all spelling variants, but preserve E (electric field) vs e.
    std::string lower = name;
    std::transform(lower.begin(),lower.end(),lower.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    if (name != "E" && constants().find(lower)!=constants().end()) return false;
    return true;
}
} // namespace

QuantityResult evaluateQuantity(const std::string &expression, const VariableTable &variables)
{
    return QuantityParser(expression,variables).run();
}

WorksheetResult evaluateWorksheet(const std::string &text)
{
    WorksheetResult result;
    std::size_t start=0;
    int lineNumber=0;
    while (start < text.size())
    {
        const auto end=text.find('\n',start);
        std::string line=trim(text.substr(start,end==std::string::npos ? std::string::npos : end-start));
        ++lineNumber;
        start=end==std::string::npos ? text.size() : end+1;
        if (line.empty() || line[0]=='#' || line.rfind("//",0)==0) continue;
        // Strip inline comments; simple worksheet syntax does not include string literals.
        const auto comment=line.find('#');
        if (comment!=std::string::npos) line=trim(line.substr(0,comment));
        if (line.empty()) continue;
        if (result.rows.size()>=128)
        {
            result.error="Worksheet limited to 128 calculation lines."; result.errorLine=lineNumber; return result;
        }
        const auto equals=line.find('=');
        std::string name,expr=line;
        if (equals!=std::string::npos)
        {
            name=trim(line.substr(0,equals)); expr=trim(line.substr(equals+1));
            if (!validVariable(name))
            {
                result.error="Invalid/reserved variable name: " + name +
                    ". Unit symbols and physical constants are reserved; use a distinct name (e.g. force instead of F).";
                result.errorLine=lineNumber; return result;
            }
            if (result.variables.find(name)!=result.variables.end())
            {
                result.error="Variable already defined in this worksheet: " + name;
                result.errorLine=lineNumber; return result;
            }
        }
        const auto outcome=evaluateQuantity(expr,result.variables);
        if (!outcome.ok)
        {
            result.error=outcome.error;
            result.errorPosition=outcome.errorPosition+(equals==std::string::npos ? 0 : equals+1);
            result.errorLine=lineNumber;
            return result;
        }
        result.rows.push_back({lineNumber,name,expr,outcome.quantity});
        if (!name.empty()) result.variables.emplace(name,outcome.quantity);
    }
    if (result.rows.empty()) {result.error="No calculation lines in the worksheet.";return result;}
    result.ok=true;
    return result;
}

std::string quantityUnit(const Quantity &q)
{
    const std::pair<D,const char *> common[] = {
        {dimensionless,"1"},{metre,"m"},{kilogram,"kg"},{second,"s"},{ampere,"A"},
        {kelvin,"K"},{mole,"mol"},{candela,"cd"},{coulomb,"C"},{hertz,"Hz"},
        {volt,"V"},{ohm,"Ohm"},{farad,"F"},{henry,"H"},{newton,"N"},
        {joule,"J"},{watt,"W"},{tesla,"T"},{siemens,"S"},{fieldDim,"V/m"},
        {epsDim,"F/m"},{muDim,"H/m"},{kDim,"N*m^2/C^2"},
        {D{0,1,-1,0,0,0,0},"m/s"},{D{0,1,-2,0,0,0,0},"m/s^2"},
        {D{0,2,0,0,0,0,0},"m^2"},{D{0,3,0,0,0,0,0},"m^3"},
        {D{1,-1,-2,0,0,0,0},"J/m^3"},{D{1,0,-3,0,0,0,0},"W/m^2"}
    };
    for (const auto &entry :common)
        if (sameD(q.dimensions,entry.first)) return entry.second;
    const char *base[]={"kg","m","s","A","K","mol","cd"};
    std::string unit;
    for (std::size_t i=0;i<7;++i)
    {
        if (std::fabs(q.dimensions[i])<1e-10) continue;
        if (!unit.empty()) unit+='*';
        unit+=base[i];
        if (std::fabs(q.dimensions[i]-1.0)>1e-10)
        {
            unit+='^';
            const double exponent=q.dimensions[i];
            if (std::fabs(exponent-std::round(exponent))<1e-10)
                unit+=std::to_string(static_cast<long long>(std::llround(exponent)));
            else
            {
                std::string formatted=std::to_string(exponent);
                formatted.erase(formatted.find_last_not_of('0')+1);
                if (!formatted.empty() && formatted.back()=='.') formatted.pop_back();
                unit+=formatted;
            }
        }
    }
    return unit.empty() ? "1" : unit;
}

std::vector<std::string> supportedUnitSymbols()
{
    std::vector<std::string> items;
    for (const auto &item:units()) items.push_back(item.first);
    std::sort(items.begin(),items.end());
    return items;
}
} // namespace EmCalculator
