#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "m_ctype.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "my_sys.h"
#include "my_config.h"
#include "my_compiler.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_loglevel.h"
#include "my_macros.h"

#include "str_uca_type.h"

void printWeightsForCodepoint(FILE *f, int codepoint, uint16 **weights) {
  uint16 *page = weights[codepoint >> 8];
  if (page == NULL)
    return;

  fprintf(f, "%s\"U+%04X\":[", codepoint > 0 ? ",\n\t\t" : "\n\t\t", codepoint);

  int offset = codepoint & 0xFF;
  int cecount = page[offset];

  for (int ce = 0; ce < cecount; ce++) {
    int weight1 = page[256 + (ce*3+0)*256+offset];
    int weight2 = page[256 + (ce*3+1)*256+offset];
    int weight3 = page[256 + (ce*3+2)*256+offset];
    fprintf(f, "%s[%d,%d,%d]", ce > 0 ? "," : "", weight1, weight2, weight3);
  }
  fprintf(f, "]");
}

void print_array_file(const uchar *arr, size_t len, FILE *f) {
  fprintf(f, "[");
  for (size_t i = 0; i < len; ++i) {
    fprintf(f, " %u", arr[i]);
    fprintf(f, (i + 1 < len) ? "," : "");
  }
  fprintf(f, "]");
}

void print_array16_file(const uint16 *arr, size_t len, FILE *f) {
  fprintf(f, "[");
  for (size_t i = 0; i < len; ++i) {
    fprintf(f, " %u", arr[i]);
    fprintf(f, (i + 1 < len) ? "," : "");
  }
  fprintf(f, "]");
}

static CHARSET_INFO *init_collation(const char *name) {
  MY_CHARSET_LOADER loader;
  my_charset_loader_init_mysys(&loader);
  return my_collation_get_by_name(&loader, name, MYF(0));
}

#define MY_UCA_MAXCHAR (0x10FFFF + 1)
#define MY_UCA_CHARS_PER_PAGE 256

int main() {
  // Load one collation to get everything going.
  init_collation("utf8mb4_0900_ai_ci");
  for (const CHARSET_INFO *cs : all_charsets) {
    if (cs && (cs->state & MY_CS_AVAILABLE)) {
      CHARSET_INFO *cs_loaded = init_collation(cs->name);
      std::string name = cs_loaded->name;
      std::string filename = "weights/" + name + ".json";
      FILE *fl = fopen(filename.c_str(), "w");
      fprintf(fl, "{\n");

      fprintf(fl, "\t\"Name\": \"%s\",\n", cs_loaded->name);
      fprintf(fl, "\t\"Charset\": \"%s\",\n", cs_loaded->csname);
      fprintf(fl, "\t\"Binary\": \"%s\",\n", (cs_loaded->state & MY_CS_BINSORT) ? "true" : "false");
      fprintf(fl, "\t\"Number\": %u", cs_loaded->number);

      if (cs->ctype != NULL) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"CType\": ");
        print_array_file(cs_loaded->ctype, 256, fl);
      }

      if (cs->to_lower != NULL) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"ToLower\": ");
        print_array_file(cs_loaded->to_lower, 256, fl);
      }

      if (cs->to_upper != NULL) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"ToUpper\": ");
        print_array_file(cs_loaded->to_upper, 256, fl);
      }

      if (cs->tab_to_uni != NULL) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"TabToUni\": ");
        print_array16_file(cs_loaded->tab_to_uni, 256, fl);
      }

      if (cs_loaded->sort_order != NULL) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"SortOrder\": ");
        print_array_file(cs_loaded->sort_order, 256, fl);
      }

      if (name.find("0900") != std::string::npos) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"Weights\":{");
        if (cs_loaded->uca != NULL) {
          for (int cp = 0; cp < MY_UCA_MAXCHAR; cp++) {
            printWeightsForCodepoint(fl, cp, cs_loaded->uca->weights);
          }
        }
        fprintf(fl, "\n\t}");
      }
      fprintf(fl, "\n}");
      fclose(fl);
    }
  }
}
