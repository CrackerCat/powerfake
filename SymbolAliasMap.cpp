/*
 * SymbolAliasMap.cpp
 *
 *  Created on: ۱۸ مهر ۱۳۹۶
 *
 *  Copyright Hedayat Vatankhah 2017.
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *     (See accompanying file LICENSE_1_0.txt or copy at
 *           http://www.boost.org/LICENSE_1_0.txt)
 */

#include "SymbolAliasMap.h"

#include <iostream>
#include <fstream>
#include <string>
#include "powerfake.h"
#include "ParseUtils.h"

using namespace PowerFake;
using namespace std;


SymbolAliasMap::SymbolAliasMap(Functions &functions, bool approximate_matching,
    bool verbose, bool verify_mode) :
        functions(functions), approximate_matching(approximate_matching),
        verbose(verbose), verify_mode(verify_mode)
{
    CreateFunctionMap(functions);
}

void SymbolAliasMap::Load(std::string_view filename)
{
    ifstream in(filename.data());
    internal::FunctionInfo fi;

    if (in && verbose)
        cout << "Looking for symbol names in symbol cache"
            << "\n-----------------------------------------------\n";
    while (in >> fi.symbol)
    {
        in >> fi.prototype.name >> fi.prototype.qual;
        in.ignore(10, ' ');
        std::getline(in, fi.prototype.params);
        std::getline(in, fi.prototype.return_type);

        auto name = GetSimpleName(fi.prototype);
        auto range = unresolved_functions.equal_range(string(name));
        for (auto p = range.first; p != range.second; )
        {
            const auto &proto = p->second->prototype;
            if (fi.prototype.name == proto.name
                    && fi.prototype.params == proto.params
                    && fi.prototype.qual == proto.qual
                    && fi.prototype.return_type == proto.return_type)
            {
                if (verbose)
                    cout << "Found symbol for " << proto.Str() << " == "
                        << fi.symbol << '\n';
                p->second->symbol = fi.symbol;
                p = verify_mode ? std::next(p) : unresolved_functions.erase(p);
            }
            else
                ++p;
        }
    }
}

void SymbolAliasMap::Save(std::string_view filename)
{
    ofstream of(filename.data());
    for (auto [name, it]: functions_map)
    {
        const internal::FunctionInfo &fi = *it;
        of << fi.symbol << ' ' << fi.prototype.name << ' ' << fi.prototype.qual
                << ' ' << fi.prototype.params << '\n';
        of << fi.prototype.return_type << '\n';
    }
}

/**
 * For each symbol in the main library, finds its alias if it is wrapped and
 * inserts the alias and the actual symbol of the target function in sym_map.
 *
 * @param symbol_name the name of a symbol in main library, which might be faked
 */
void SymbolAliasMap::AddSymbol(const char *symbol_name)
{
    std::string demangled = boost::core::demangle(symbol_name);
    FindWrappedSymbol(demangled, symbol_name);
}

/**
 * @return if all wrapped symbols were found
 */
bool SymbolAliasMap::FoundAllWrappedSymbols() const
{
    if (!verify_mode)
        return unresolved_functions.empty();

    bool found_all = true;
    for (const auto &wfp: functions)
    {
        if (wfp.symbol.empty())
        {
            found_all = false;
            const auto &wf = wfp.prototype;
            cerr << "Error: Cannot find symbol for function: "
                    << wf.return_type << ' ' << wf.name << wf.params
                    << " (alias: " << wf.alias << ")" << endl;
        }
    }
    return found_all;
}

void SymbolAliasMap::PrintUnresolvedSymbols()
{
    if (verify_mode)
    {
        for (const auto &wfp: functions)
        {
            if (wfp.symbol.empty())
            {
                const auto &wf = wfp.prototype;
                cerr << "Error: Cannot find symbol for function: "
                        << wf.Str() << " (alias: " << wf.alias << ")" << endl;
            }
        }
    }
    else
    {
        for (auto [sn, it]: unresolved_functions)
        {
            const auto &wf = it->prototype;
            cerr << "Error: Cannot find symbol for function: "
                    << wf.Str() << " (alias: " << wf.alias << ")" << endl;
        }
    }
}

void SymbolAliasMap::ApplyApproximateMatching()
{
    if (!approximate_matching)
        return;

    unsigned prev_unresolved = unresolved_functions.size() + 1;
    while (!unresolved_functions.empty() && prev_unresolved > unresolved_functions.size())
    {
        prev_unresolved = unresolved_functions.size();
        bool erase = false;
        for (auto it = unresolved_functions.begin();
                it != unresolved_functions.end();
                it = erase ? unresolved_functions.erase(it) : std::next(it))
        {
            erase = false;
            if (!it->second->symbol.empty())
            {
                erase = true;
                continue;
            }
            auto &v = candidates[it->second->prototype.name];
            if (v.empty())
                continue;

            if (v.size() == 1)
            {
                it->second->symbol = v.back().symbol;
                v.clear();
                erase = true;
                continue;
            }
        }
    }
}

void SymbolAliasMap::CreateFunctionMap(Functions &functions)
{
    for (auto it = functions.begin(); it != functions.end(); ++it)
        functions_map.insert({ GetSimpleName(it->prototype), it });
    unresolved_functions = functions_map;
}

std::string_view SymbolAliasMap::GetSimpleName(
    const PowerFake::internal::FunctionPrototype &prototype)
{
    std::string_view name = prototype.name;
    auto nstart = name.rfind(':', prototype.name.length()-1);
    if (nstart != std::string::npos)
        return name.substr(nstart + 1);
    return name;
}

/**
 * For a given symbol and its demangled name, finds corresponding prototype
 * from @a protos set and stores the mapping
 * @param protos all wrapped function prototypes
 * @param demangled the demangled form of @a symbol_name
 * @param symbol_name a symbol in the object file
 */
void SymbolAliasMap::FindWrappedSymbol(const std::string &demangled,
    const char *symbol_name)
{
    if (!IsFunction(symbol_name, demangled))
        return;

    unsigned name_start, name_end;
    auto name = FunctionName(demangled, name_start, name_end);

    auto range = unresolved_functions.equal_range(name);
    for (auto p = range.first; p != range.second; ++p)
    {
        auto func = p->second->prototype;
        if (IsSameFunction(demangled, func))
        {
            const string sig = func.name + func.params;
            if (verify_mode && !p->second->symbol.empty()
                    && p->second->symbol != symbol_name)
                throw runtime_error(
                    "Error: (BUG) duplicate symbols found for: "
                            + func.return_type + ' ' + sig + ":\n"
                            + '\t' + p->second->symbol + '\n'
                            + '\t' + symbol_name);

            p->second->symbol = symbol_name;
            if (verbose)
                cout << "Found symbol for " << func.return_type << ' ' << sig
                        << " == " << symbol_name << " (" << demangled << ") \n";
            if (approximate_matching)
            {
                auto &v = candidates[func.name];
                auto it = std::remove_if(v.begin(), v.end(),
                    [symbol_name](const auto &p)
                    {
                        return p.symbol == symbol_name;
                    });
                v.erase(it, v.end());
            }
            if (!verify_mode)
            {
                unresolved_functions.erase(p);
                break;
            }
        }
        else if (approximate_matching)
        {
            auto proto = ParseDemangledFunction(demangled, name_start, name_end);
            if (proto.name == func.name
                    && proto.qual == (func.qual & ~internal::Qualifiers::NOEXCEPT)
                    && proto.expanded_params.size()
                            == SplitParams(func.params).size())
            {
                auto &v = candidates[proto.name];
                auto it = std::find_if(v.begin(), v.end(),
                    [symbol_name](const auto &p)
                    {
                        return p.symbol == symbol_name;
                    });
                if (it == v.end())
                {
                    proto.symbol = symbol_name;
                    v.push_back(proto);
                    if (verbose)
                        cout << "Mark " << demangled
                            << " as candidate for matching: " << func.Str()
                            << endl;
                }
            }
        }
    }
}

bool SymbolAliasMap::IsFunction(const char *symbol_name  [[maybe_unused]],
    const std::string &demangled)
{
    // detect static variables inside functions, which are demangled in
    // this format: function_name()[ cv-qualification]::static_var_name
    auto params_end = demangled.rfind(')');
    if (params_end != string::npos)
    {
        auto static_var_separator = demangled.find("::", params_end);
        if (static_var_separator != string::npos
                && demangled.find('(', static_var_separator) == string::npos)
            return false;
    }
    return true;
}

bool SymbolAliasMap::IsSameFunction(const std::string &demangled,
    const PowerFake::internal::FunctionPrototype &proto)
{
    const string base_sig = proto.name + proto.params;
    string qs = internal::ToStr(proto.qual, true);
    if (!qs.empty())
        qs = ' ' + qs;

    if (demangled == proto.name) // C functions
        return true;
    if (demangled == base_sig + qs)   // Normal C++ functions
        return true;

    // template functions also have return type
    if (demangled == proto.return_type + ' ' + base_sig + qs)
        return true;

    // signatures with an abi tag, e.g. func[abi:cxx11](int)
    bool prefix_found = (demangled.find(proto.name + "[") == 0
            || demangled.find(proto.return_type + ' ' + proto.name + "[") == 0);
    if (prefix_found)
    {
        auto postfix_pos = demangled.find("]" + proto.params + qs);
        if (postfix_pos != string::npos)
            return demangled.size() - postfix_pos
                    == (proto.params + qs).size() + 1;
    }

    // TODO: Warn for similar symbols, e.g. <signature> [clone .cold]

    return false;
}
