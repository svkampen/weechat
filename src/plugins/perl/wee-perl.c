/*
 * Copyright (c) 2003 by FlashCode <flashcode@flashtux.org>
 *                       Bounga <bounga@altern.org>
 *                       Xahlexx <xahlexx@tuxisland.org>
 * See README for License detail.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* wee-perl.c: Perl plugin support for WeeChat */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "../../common/weechat.h"
#include "wee-perl.h"
#include "../../gui/gui.h"


static PerlInterpreter *my_perl = NULL;
static t_perl_script *perl_scripts = NULL;

extern void boot_DynaLoader (pTHX_ CV* cv);


/*
 * IRC::register: startup function for all WeeChat Perl scripts
 */

static XS (XS_IRC_register)
{
    char *name, *version, *shutdown_func, *description;
    int integer;
    t_perl_script *new_perl_script;
    dXSARGS;
    
    name = SvPV (ST (0), integer);
    version = SvPV (ST (1), integer);
    shutdown_func = SvPV (ST (2), integer);
    description = SvPV (ST (3), integer);
    
    new_perl_script = (t_perl_script *)malloc (sizeof (t_perl_script));
    if (new_perl_script)
    {
        new_perl_script->name = strdup (name);
        new_perl_script->version = strdup (version);
        new_perl_script->shutdown_func = strdup (shutdown_func);
        new_perl_script->description = strdup (description);
        new_perl_script->next_script = perl_scripts;
        perl_scripts = new_perl_script;
        wee_log_printf (_("registered Perl script: \"%s\"\n"), name);
    }
    else
        gui_printf (NULL,
                    _("%s unable to load Perl script \"%s\"\n"),
                    WEECHAT_ERROR, name);
    XST_mPV (0, VERSION);
    XSRETURN (1);
}

/*
 * IRC::print: print message to current window
 */

static XS (XS_IRC_print)
{
    int i, integer;
    char *message;
    dXSARGS;
    
    for (i = 0; i < items; i++)
    {
        message = SvPV (ST (i), integer);
        gui_printf (NULL, "%s\n", message);
    }
    
    XSRETURN_EMPTY;
}

/*
 * xs_init: initialize subroutines
 */

void
xs_init (pTHX)
{
    newXS ("DynaLoader::boot_DynaLoader", boot_DynaLoader, __FILE__);
    newXS ("IRC::register", XS_IRC_register, "IRC");
    newXS ("IRC::print", XS_IRC_print, "IRC");
}

/*
 * wee_perl_init: initialize Perl interface for WeeChat
 */

void
wee_perl_init ()
{
    char *perl_args[] = { "", "-e", "0" };
    /* This Perl code is extracted/modified from X-Chat IRC client */
    /* X-Chat is (c) 1998-2002 Peter Zelezny */
    char *weechat_perl_func =
    {
        "sub wee_perl_load_file"
        "{"
        "    my $filename = shift;"
        "    local $/ = undef;"
        "    open FILE, $filename or return \"__WEECHAT_ERROR__\";"
        "    $_ = <FILE>;"
        "    close FILE;"
        "    return $_;"
        "}"
        "sub wee_perl_load_eval_file"
        "{"
        "    my $filename = shift;"
        "    my $content = wee_perl_load_file ($filename);"
        "    if ($content eq \"__WEECHAT_ERROR__\")"
        "    {"
        "        IRC::print \"" WEECHAT_ERROR " Perl script '$filename' not found.\\n\";"
        "        return 1;"
        "    }"
        "    eval $content;"
        "    if ($@)"
        "    {"
        "        IRC::print \"" WEECHAT_ERROR " unable to load Perl script '$filename':\\n\";"
        "        IRC::print \"$@\\n\";"
        "        return 2;"
        "    }"
        "    return 0;"
        "}"
        "$SIG{__WARN__} = sub { IRC::print \"$_[0]\n\"; };"
    };
    
    my_perl = perl_alloc ();
    perl_construct (my_perl);
    perl_parse (my_perl, xs_init, 3, perl_args, NULL);
    eval_pv (weechat_perl_func, TRUE);
}

/*
 * wee_perl_search: search a (loaded) Perl script by name
 */

t_perl_script *
wee_perl_search (char *name)
{
    t_perl_script *ptr_perl_script;
    
    for (ptr_perl_script = perl_scripts; ptr_perl_script;
         ptr_perl_script = ptr_perl_script->next_script)
    {
        if (strcmp (ptr_perl_script->name, name) == 0)
            return ptr_perl_script;
    }
    
    /* script not found */
    return NULL;
}

/*
 * wee_perl_exec: execute a Perl script
 */

int
wee_perl_exec (char *function, char *arguments)
{
    char *argv[2];
    int count, return_code;
    SV *sv;
    
    /* call Perl function */
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(sp);
    argv[0] = arguments;
    argv[1] = NULL;
    count = perl_call_argv (function, G_EVAL | G_SCALAR, argv);
    SPAGAIN;
    
    /* check if ok */
    sv = GvSV (gv_fetchpv ("@", TRUE, SVt_PV));
    return_code = 1;
    if (SvTRUE (sv))
    {
        gui_printf (NULL,
                    _("Perl error: %s\n"),
                    SvPV (sv, count));
        POPs;
    }
    else
    {
        if (count != 1)
        {
            gui_printf (NULL,
                        _("Perl error: too much values from \"%s\" (%d). Expected: 1.\n"),
                        function, count);
        }
        else
            return_code = POPi;
    }
    
    PUTBACK;
    FREETMPS;
    LEAVE;
    
    return return_code;
}

/*
 * wee_perl_load: load a Perl script
 */

int
wee_perl_load (char *filename)
{
    /* execute Perl script */
    wee_log_printf (_("loading Perl script \"%s\"\n"), filename);
    return wee_perl_exec ("wee_perl_load_eval_file", filename);
}

/*
 * wee_perl_unload: unload a Perl script
 */

void
wee_perl_unload (t_perl_script *ptr_perl_script)
{
    if (ptr_perl_script)
    {
        wee_log_printf (_("unloading Perl script \"%s\"\n"),
                        ptr_perl_script->name);
        
        /* call shutdown callback function */
        if (ptr_perl_script->shutdown_func[0])
            wee_perl_exec (ptr_perl_script->shutdown_func, "");
        
        /* free data */
        if (ptr_perl_script->name)
            free (ptr_perl_script->name);
        if (ptr_perl_script->version)
            free (ptr_perl_script->version);
        if (ptr_perl_script->shutdown_func)
            free (ptr_perl_script->shutdown_func);
        if (ptr_perl_script->description)
            free (ptr_perl_script->description);
    }
}

/*
 * wee_perl_unload_all: unload all Perl scripts
 */

void
wee_perl_unload_all ()
{
    t_perl_script *ptr_perl_script;
    
    while (perl_scripts)
    {
        wee_perl_unload (perl_scripts);
        ptr_perl_script = perl_scripts->next_script;
        free (perl_scripts);
        perl_scripts = ptr_perl_script;
    }
}

/*
 * wee_perl_end: shutdown Perl interface
 */

void
wee_perl_end ()
{
    /* unload all scripts */
    wee_perl_unload_all ();
    
    /* free Perl interpreter */
    if (my_perl)
    {
        perl_destruct (my_perl);
        perl_free (my_perl);
    }
}
