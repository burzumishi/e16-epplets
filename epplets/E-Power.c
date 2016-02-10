#define _GNU_SOURCE
#include "epplet.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* Modified by Attila ZIMLER <hijaszu@hlfslinux.hu>, 2003/11/16
   Added ACPI power management support.
*/

typedef struct
{
   char                design_cap_unknown;
   char                last_full_unknown;
   char                rate_unknown;
   char                level_unknown;

   char                discharging;
   char                charging;
   char                battery;

   int                 bat_max;
   int                 bat_filled;
   int                 bat_level;
   int                 bat_drain;
} bat_info_t;

typedef void        (bi_fetch_f) (bat_info_t * bi);

#define MODE_APM  1
#define MODE_ACPI 2
#define MODE_SYS  3
static char         power_mode = 0;

static Epplet_gadget b_close, b_suspend, b_sleep, b_help, image, label;

static void
cb_timer_apm(void)
{
   static int          prev_bat_val = 110;
   static int          bat_val = 0;
   static int          time_val = 0;

   static int          prev_up[16] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
   };
   static int          prev_count = 0;

   FILE               *f;

   f = fopen("/proc/apm", "r");
   if (f)
     {
	char                s[256], s1[32], s2[32], s3[32];
	int                 apm_flags, ac_stat, bat_stat, bat_flags;
	int                 i, hours, minutes, up, up2;
	char               *s_ptr;

	fgets(s, 255, f);
	sscanf(s, "%*s %*s %x %x %x %x %s %s %s", &apm_flags, &ac_stat,
	       &bat_stat, &bat_flags, s1, s2, s3);
	s1[strlen(s1) - 1] = 0;
	bat_val = atoi(s1);
	if (!strcmp(s3, "sec"))
	   time_val = atoi(s2);
	else if (!strcmp(s3, "min"))
	   time_val = atoi(s2) * 60;
	fclose(f);

	up = bat_val - prev_bat_val;
	up2 = up;
	for (i = 0; i < 16; i++)
	   up2 = +prev_up[i];
	up2 = (up2 * 60) / 17;

	prev_up[prev_count] = up;

	prev_count++;
	if (prev_count >= 16)
	   prev_count = 0;

	s_ptr = s;

	if (bat_flags != 0xff && bat_flags & 0x80)
	  {
	     s_ptr += sprintf(s_ptr, "no battery");
	  }
	else
	  {
	     if (bat_val > 0)
		s_ptr += sprintf(s_ptr, "%i%%", bat_val);

	     switch (bat_stat)
	       {
	       case 0:
		  s_ptr += sprintf(s_ptr, ", high");
		  break;
	       case 1:
		  s_ptr += sprintf(s_ptr, ", low");
		  break;
	       case 2:
		  s_ptr += sprintf(s_ptr, ", crit.");
		  break;
	       case 3:
		  s_ptr += sprintf(s_ptr, ", charge");
		  break;
	       }
	  }
	s_ptr += sprintf(s_ptr, "\n");

	if (ac_stat == 1)
	  {
	     s_ptr += sprintf(s_ptr, "AC on-line");
	  }
	else
	  {
	     hours = time_val / 3600;
	     minutes = (time_val / 60) % 60;
	     if (up2 > 0)
		s_ptr += sprintf(s_ptr, "(%i:%02i)\n%i:%02i",
				 (((100 - bat_val) * 2 * 60) / up2) / 60,
				 (((100 - bat_val) * 2 * 60) / up2) % 60,
				 hours, minutes);
	     else
		s_ptr += sprintf(s_ptr, "%i:%02i", hours, minutes);
	  }
	Epplet_change_label(label, s);

	sprintf(s, "E-Power-Bat-%i.png", ((bat_val + 5) / 10) * 10);
	Epplet_change_image(image, 44, 24, s);

	prev_bat_val = bat_val;
     }
}

static void
_bat_info_fetch_acpi(bat_info_t * bi)
{
   FILE               *f;
   DIR                *dirp;
   struct dirent      *dp;
   char               *line = NULL;
   size_t              lsize = 0;

   /* Read some information on first run. */
   dirp = opendir("/proc/acpi/battery");
   if (!dirp)
      return;

   while ((dp = readdir(dirp)))
     {
	char                buf[4096];

	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
	   continue;

	snprintf(buf, sizeof(buf), "/proc/acpi/battery/%s/info", dp->d_name);
	f = fopen(buf, "r");
	if (f)
	  {
	     int                 design_cap = 0;
	     int                 last_full = 0;

	     getline(&line, &lsize, f);
	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s %*s", buf);
	     if (!strcmp(buf, "unknown"))
		bi->design_cap_unknown = 1;
	     else
		sscanf(line, "%*[^:]: %i %*s", &design_cap);
	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s %*s", buf);
	     if (!strcmp(buf, "unknown"))
		bi->last_full_unknown = 1;
	     else
		sscanf(line, "%*[^:]: %i %*s", &last_full);
	     fclose(f);
	     bi->bat_max += design_cap;
	     bi->bat_filled += last_full;
	  }

	snprintf(buf, sizeof(buf), "/proc/acpi/battery/%s/state", dp->d_name);
	f = fopen(buf, "r");
	if (f)
	  {
	     char                present[256];
	     char                capacity_state[256];
	     char                charging_state[256];
	     int                 rate = 0;
	     int                 level = 0;

	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s", present);
	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s", capacity_state);
	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s", charging_state);
	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s %*s", buf);
	     if (!strcmp(buf, "unknown"))
		bi->rate_unknown = 1;
	     else
		sscanf(line, "%*[^:]: %i %*s", &rate);
	     getline(&line, &lsize, f);
	     sscanf(line, "%*[^:]: %250s %*s", buf);
	     if (!strcmp(buf, "unknown"))
		bi->level_unknown = 1;
	     else
		sscanf(line, "%*[^:]: %i %*s", &level);
	     fclose(f);

	     if (!strcmp(present, "yes"))
		bi->battery++;
	     if (!strcmp(charging_state, "discharging"))
		bi->discharging++;
	     if (!strcmp(charging_state, "charging"))
		bi->charging++;
	     bi->bat_drain += rate;
	     bi->bat_level += level;
	  }
     }

   closedir(dirp);
   free(line);
}

static void
_bat_info_fetch_sys(bat_info_t * bi)
{
   DIR                *dirp;
   struct dirent      *dp;
   FILE               *f;
   char               *line = NULL;
   size_t              lsize = 0;

   /* Read some information on first run. */
   dirp = opendir("/sys/class/power_supply/");
   if (!dirp)
      return;

   while ((dp = readdir(dirp)))
     {
	char                buf[4096];

	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") ||
	    !strstr(dp->d_name, "BAT"))
	   continue;

	snprintf(buf, sizeof(buf), "/sys/class/power_supply/%s/uevent",
		 dp->d_name);
	f = fopen(buf, "r");
	if (f)
	  {
	     int                 design_cap = 0;
	     int                 last_full = 0;
	     char                present[256];
	     char                key[256];
	     char                value[256];
	     char                charging_state[256];
	     int                 rate = 0;
	     int                 level = 0;

	     while (getline(&line, &lsize, f) != -1)
	       {
		  sscanf(line, "%[^=]= %250s", key, value);

		  if (strcmp(key, "POWER_SUPPLY_NAME") == 0)
		    {
		    }
		  else if (strcmp(key, "POWER_SUPPLY_STATUS") == 0)
		    {
		       sscanf(value, "%250s", charging_state);
		       if (!strcmp(charging_state, "Discharging"))
			  bi->discharging++;
		       if (!strcmp(charging_state, "Charging"))
			  bi->charging++;
		    }
		  else if (strcmp(key, "POWER_SUPPLY_PRESENT") == 0)
		    {
		       sscanf(value, "%250s", present);
		       if (!strcmp(present, "1"))
			  bi->battery++;
		    }
		  else if (strcmp(key, "POWER_SUPPLY_CURRENT_NOW") == 0)
		    {
		       sscanf(value, "%i", &rate);
		       bi->rate_unknown = 0;
		       bi->bat_drain += rate;
		    }
		  else if (strcmp(key, "POWER_SUPPLY_CHARGE_FULL_DESIGN") == 0)
		    {
		       sscanf(value, "%i", &design_cap);
		       bi->design_cap_unknown = 0;
		       bi->bat_max += design_cap;
		    }
		  else if (strcmp(key, "POWER_SUPPLY_CHARGE_FULL") == 0)
		    {
		       sscanf(value, "%i", &last_full);
		       bi->last_full_unknown = 0;
		       bi->bat_filled += last_full;
		    }
		  else if (strcmp(key, "POWER_SUPPLY_CHARGE_NOW") == 0)
		    {
		       sscanf(value, "%i", &level);
		       bi->level_unknown = 0;
		       bi->bat_level += level;
		    }
	       }
	     fclose(f);
	  }
     }
   closedir(dirp);
   free(line);
}

static void
cb_timer_gen(bi_fetch_f * bi_fetch)
{
   /* We don't have any data from the remaining percentage, and time directly,
    * so we have to calculate and measure them.
    * (Measure the time and calculate the percentage.)
    */
   bat_info_t          bi;
   int                 bat_val;
   char                current_status[256];
   int                 hours, minutes;
   const char         *pwr;

   memset(&bi, 0, sizeof(bi));
   bi_fetch(&bi);

   if (bi.bat_filled > 0)
      bat_val = (100 * bi.bat_level) / bi.bat_filled;
   else
      bat_val = 100;

   minutes = 0;
   if (bi.bat_drain > 0)
     {
	if (bi.discharging)
	   minutes = (60 * bi.bat_level) / bi.bat_drain;
	else if (bi.charging)
	   minutes = (60 * (bi.bat_filled - bi.bat_level)) / bi.bat_drain;
     }
   hours = minutes / 60;
   minutes -= (hours * 60);

   pwr = bi.charging ? " PWR" : "";
   if (!bi.battery)
      snprintf(current_status, sizeof(current_status), "No Bat");
   else if (bi.level_unknown)
      snprintf(current_status, sizeof(current_status),
	       "Level ???\n" "Bad Driver");
   else if (bat_val >= 100 && bi.bat_drain <= 0)
      snprintf(current_status, sizeof(current_status), "Full");
   else if (bi.rate_unknown || bi.bat_drain <= 0)
      snprintf(current_status, sizeof(current_status),
	       "%i%%%s\n" "Time ???", bat_val, pwr);
   else if (bi.charging || bi.discharging)
      snprintf(current_status, sizeof(current_status),
	       "%i%%%s\n" "%02i:%02i", bat_val, pwr, hours, minutes);
   else
      snprintf(current_status, sizeof(current_status), "???");
   Epplet_change_label(label, current_status);

   if (bat_val > 100)
      bat_val = 100;
   sprintf(current_status, "E-Power-Bat-%i.png", ((bat_val + 5) / 10) * 10);
   Epplet_change_image(image, 44, 24, current_status);
}

static void
cb_timer(void *data)
{
   struct stat         st;

   if (power_mode == 0)
     {
	if ((stat("/sys/class/power_supply", &st) > -1) && S_ISDIR(st.st_mode))
	   power_mode = MODE_SYS;
	else if ((stat("/proc/acpi/battery", &st) > -1) && S_ISDIR(st.st_mode))
	   power_mode = MODE_ACPI;
	if ((stat("/proc/apm", &st) > -1) && S_ISREG(st.st_mode))
	   power_mode = MODE_APM;
     }

   if (power_mode == MODE_APM)
      cb_timer_apm();
   else if (power_mode == MODE_ACPI)
      cb_timer_gen(_bat_info_fetch_acpi);
   else if (power_mode == MODE_SYS)
      cb_timer_gen(_bat_info_fetch_sys);

   Epplet_timer(cb_timer, NULL, 10.0, "TIMER");
}

static void
cb_close(void *data)
{
   Epplet_unremember();
   Esync();
   exit(0);
}

static void
cb_in(void *data, Window w)
{
   Epplet_gadget_show(b_close);
   Epplet_gadget_show(b_suspend);
   Epplet_gadget_show(b_sleep);
   Epplet_gadget_show(b_help);
}

static void
cb_out(void *data, Window w)
{
   Epplet_gadget_hide(b_close);
   Epplet_gadget_hide(b_suspend);
   Epplet_gadget_hide(b_sleep);
   Epplet_gadget_hide(b_help);
}

static void
cb_help(void *data)
{
   Epplet_show_about("E-Power");
}

static void
cb_suspend(void *data)
{
   system("/usr/bin/apm -s");
}

static void
cb_sleep(void *data)
{
   system("/usr/bin/apm -S");
}

int
main(int argc, char **argv)
{
   char               *s;

   s = getenv("E_Power_Mode");
   if (s)
      power_mode = atoi(s);

   Epplet_Init("E-Power", "0.1", "Enlightenment Laptop Power Epplet",
	       3, 3, argc, argv, 0);
   atexit(Epplet_cleanup);
   Epplet_timer(cb_timer, NULL, 10.0, "TIMER");
   b_close = Epplet_create_button(NULL, NULL,
				  2, 2, 0, 0, "CLOSE", 0, NULL, cb_close, NULL);
   b_help = Epplet_create_button(NULL, NULL,
				 34, 2, 0, 0, "HELP", 0, NULL, cb_help, NULL);
   b_suspend = Epplet_create_button(NULL, NULL,
				    2, 34, 0, 0, "PAUSE", 0, NULL,
				    cb_suspend, NULL);
   b_sleep = Epplet_create_button(NULL, NULL,
				  34, 34, 0, 0, "STOP", 0, NULL,
				  cb_sleep, NULL);
   Epplet_gadget_show(image = Epplet_create_image(2, 2, 44, 24,
						  "E-Power-Bat-100.png"));
   Epplet_gadget_show(label =
		      Epplet_create_label(2, 28, "APM, ACPI\nmissing", 1));
   Epplet_register_focus_in_handler(cb_in, NULL);
   Epplet_register_focus_out_handler(cb_out, NULL);
   cb_timer(NULL);
   Epplet_show();
   Epplet_Loop();
   return 0;
}
