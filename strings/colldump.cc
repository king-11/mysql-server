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

static void print_contractions_1(FILE *f, my_wc_t *buf, size_t depth, bool *first, bool contextual, const MY_CONTRACTION &contraction) {
  buf[depth] = contraction.ch;

  if (contraction.is_contraction_tail) {
    if (!*first) {
      fprintf(f, ",");
    }

    *first = false;
    fprintf(f, "{\"Path\" : [");
    for (size_t i = 0; i <= depth; i++) {
      fprintf(f, "%s%d", i > 0 ? "," : "", (unsigned int)buf[i]);
    }
    fprintf(f, "], \"Weights\" : [");
    for (size_t i = 0; i < MY_UCA_MAX_WEIGHT_SIZE; i++) {
      fprintf(f, "%s%d", i > 0 ? "," : "", contraction.weight[i]);
    }
    fprintf(f, "]");
    if (contextual) {
      fprintf(f, ", \"Contextual\" : true");
    }
    fprintf(f, "}");
  }

  for (const MY_CONTRACTION &ctr : contraction.child_nodes) {
    print_contractions_1(f, buf, depth+1, first, false, ctr);
  }
  for (const MY_CONTRACTION &ctr : contraction.child_nodes_context) {
    print_contractions_1(f, buf, depth+1, first, true, ctr);
  }
}

static void print_contractions(FILE *f, std::vector<MY_CONTRACTION> *contractions) {
  my_wc_t buf[256];
  bool first = true;

  for (const MY_CONTRACTION &ctr : *contractions) {
    print_contractions_1(f, buf, 0, &first, false, ctr);
  }
}

static void print_reorder_params(FILE *f, struct Reorder_param *reorder) {
  for (int i = 0; i < reorder->wt_rec_num; i++) {
    struct Reorder_wt_rec &r = reorder->wt_rec[i];
    fprintf(f, "%s[%d, %d, %d, %d]", i > 0 ? ", " : "",
      r.old_wt_bdy.begin, r.old_wt_bdy.end,
      r.new_wt_bdy.begin, r.new_wt_bdy.end);
  }
}

static void printWeightsForCodepoint(FILE *f, int codepoint, uint16 **weights) {
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

static void print_array_file(const uchar *arr, size_t len, FILE *f) {
  fprintf(f, "[");
  for (size_t i = 0; i < len; ++i) {
    fprintf(f, " %u", arr[i]);
    fprintf(f, (i + 1 < len) ? "," : "");
  }
  fprintf(f, "]");
}

static void print_array16_file(const uint16 *arr, size_t len, FILE *f) {
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

extern MY_COLLATION_HANDLER my_collation_uca_900_handler;
extern MY_COLLATION_HANDLER my_collation_any_uca_handler;
extern MY_COLLATION_HANDLER my_collation_utf16_uca_handler;
extern MY_COLLATION_HANDLER my_collation_utf32_uca_handler;
extern MY_COLLATION_HANDLER my_collation_ucs2_uca_handler;

struct KNOWN_HANDLER {
  const char *name;
  const MY_COLLATION_HANDLER *h;
};

static KNOWN_HANDLER known_handlers[] = {
  { "8bit_bin", &my_collation_8bit_bin_handler },
  { "8bit_simple_ci", &my_collation_8bit_simple_ci_handler },
  { "any_uca", &my_collation_any_uca_handler },
  { "uca_900", &my_collation_uca_900_handler },
  { "utf16_uca", &my_collation_utf16_uca_handler },
  { "utf32_uca", &my_collation_utf32_uca_handler },
  { "ucs2_uca", &my_collation_ucs2_uca_handler },
};

int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--test")) {
    uchar src[4096];
    uchar dst[4096*4];
    CHARSET_INFO *cs = init_collation(argv[2]);
    if (cs == NULL) {
      fprintf(stderr, "unknown collation: %s", argv[2]);
      return 1;
    }
    size_t readin = fread(src, 1, 4096, stdin);
    size_t wlen = cs->coll->strnxfrm(cs, dst, sizeof(dst), 0, src, readin, 0);
    fwrite(dst, 1, wlen, stdout);
    return 0;
  }

  if (argc != 2) {
    fprintf(stderr, "usage: %s PATH_TO_DUMP\n", argv[0]);
    return 1;
  }

  char pathbuf[4096];

  // Load one collation to get everything going.
  init_collation("utf8mb4_0900_ai_ci");
  for (const CHARSET_INFO *cs : all_charsets) {
    if (cs && (cs->state & MY_CS_AVAILABLE)) {
      CHARSET_INFO *cs_loaded = init_collation(cs->name);
      snprintf(pathbuf, sizeof(pathbuf), "%s/%s.json", argv[1], cs_loaded->name);

      FILE *fl = fopen(pathbuf, "w");
      if (fl == NULL) {
        fprintf(stderr, "failed to create '%s'\n", pathbuf);
        return 1;
      }

      fprintf(fl, "{\n");
      fprintf(fl, "\t\"Name\": \"%s\",\n", cs_loaded->name);
      fprintf(fl, "\t\"Charset\": \"%s\",\n", cs_loaded->csname);
      fprintf(fl, "\t\"Binary\": %s,\n", (cs_loaded->state & MY_CS_BINSORT) ? "true" : "false");

      for (const KNOWN_HANDLER &handler : known_handlers) {
        if (cs_loaded->coll == handler.h) {
          fprintf(fl, "\t\"CollationImpl\": \"%s\",\n", handler.name);
          break;
        }
      }

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

      if (cs_loaded->uca != NULL) {
        fprintf(fl, ",\n");
        fprintf(fl, "\t\"Weights\":{");
        if (cs_loaded->uca->version == UCA_V900) {
          for (int cp = 0; cp < MY_UCA_MAXCHAR; cp++) {
            printWeightsForCodepoint(fl, cp, cs_loaded->uca->weights);
          }
        } else {
          // TODO
        }
        fprintf(fl, "\n\t}");

        if (cs_loaded->uca->have_contractions) {
          fprintf(fl, ",\n");
          fprintf(fl, "\t\"Contractions\":[\n");
          print_contractions(fl, cs_loaded->uca->contraction_nodes);
          fprintf(fl, "\n\t]");
        }
      }

      if (cs_loaded->coll_param != NULL) {
        fprintf(fl, ",\n\t\"UppercaseFirst\":%s",
          cs_loaded->coll_param->case_first == CASE_FIRST_UPPER ? "true" : "false");
        if (cs_loaded->coll_param->reorder_param != NULL) {
          fprintf(fl, ",\n");
          fprintf(fl, "\t\"Reorder\":[\n");
          print_reorder_params(fl, cs_loaded->coll_param->reorder_param);
          fprintf(fl, "\n\t]");
        }
      }

      fprintf(fl, "\n}");
      fclose(fl);
    }
  }
}
