/*
 * bind_fakes.cpp
 *
 *  Created on: ۲۶ شهریور ۱۳۹۶
 *
 *  Copyright Hedayat Vatankhah 2017.
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *     (See accompanying file LICENSE_1_0.txt or copy at
 *           http://www.boost.org/LICENSE_1_0.txt)
 */

#include <iostream>
#include <fstream>
#include <map>
#include <boost/core/demangle.hpp>

#include "powerfake.h"
#include "piperead.h"
#include "NMSymbolReader.h"
#include "SymbolAliasMap.h"

#define TO_STR(a) #a
#define BUILD_NAME_STR(pref, base, post) TO_STR(pref) + base + TO_STR(post)
#define TMP_WRAPPER_NAME_STR(name) BUILD_NAME_STR(TMP_WRAPPER_PREFIX, name, TMP_POSTFIX)
#define TMP_REAL_NAME_STR(name) BUILD_NAME_STR(TMP_REAL_PREFIX, name, TMP_POSTFIX)

using namespace std;
using namespace PowerFake;


int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "At least one base library name and one wrapping object/library"
                " are required: " << argv[0] << " <base_lib.a> <wrapper.o/.a>..."
                << endl;
        return 1;
    }

    try
    {
        bool leading_underscore = false;
        int argc_inc = 0;
        if (argv[1] == "--leading-underscore"s)
        {
            leading_underscore = true;
            argc_inc = 1;
        }

        vector<string> object_files;
        for (int i = argc_inc + 2; i < argc; ++i)
            object_files.push_back(argv[i]);

        SymbolAliasMap symmap;
        // Found real symbols which we want to wrap
        {
            PipeRead pipe("nm -o "s + argv[argc_inc + 1]);
            NMSymbolReader nm_reader(pipe.InputStream(), leading_underscore);

            const char *symbol;
            while ((symbol = nm_reader.NextSymbol()))
                symmap.AddSymbol(symbol);
        }

        // Create powerfake.link_flags containing link flags for linking
        // test binary
        {
            ofstream link_flags("powerfake.link_flags");
            for (const auto &syms: symmap.Map())
                link_flags << "-Wl,--wrap=" << syms.second << endl;
        }

        const string sym_prefix = leading_underscore ? "_" : "";
        // Rename our wrap/real symbols (which are mangled) to the ones expected
        // by ld linker
        for (const auto &objfile: object_files)
        {
            PipeRead pipe("nm -o "s + objfile);
            NMSymbolReader nm_reader(pipe.InputStream(), leading_underscore);

            string objcopy_params;
            const char *symbol;
            while ((symbol = nm_reader.NextSymbol()))
            {
                if (symbol[0] == '.')
                    continue;
                for (const auto &syms: symmap.Map())
                {
                    string wrapper_name = TMP_WRAPPER_NAME_STR(syms.first);
                    string real_name = TMP_REAL_NAME_STR(syms.first);
                    string symbol_str = symbol;
                    if (symbol_str.find(wrapper_name) != string::npos)
                    {
                        cout << "Found wrapper symbol to replace: " << symbol_str
                                << ' ' << boost::core::demangle(symbol) << endl;
                        objcopy_params += " --redefine-sym " + sym_prefix
                                + symbol_str + "=" + sym_prefix + "__wrap_"
                                + syms.second;
                    }
                    if (symbol_str.find(real_name) != string::npos)
                    {
                        cout << "Found real symbol to replace: " << symbol_str
                                << ' ' << boost::core::demangle(symbol) << endl;
                        objcopy_params += " --redefine-sym " + sym_prefix
                                + symbol_str + "=" + sym_prefix + "__real_"
                                + syms.second;
                    }
                }
            }
            if (!objcopy_params.empty())
            {
                string cmd = "objcopy" + objcopy_params + ' ' + objfile;
                system(cmd.c_str());
            }
            // todo: check return value
        }
    }
    catch (exception &e)
    {
        cerr << "Exception: " << e.what() << endl;
        return 1;
    }

    return 0;
}
