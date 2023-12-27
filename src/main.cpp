
#include <cstddef>
#include <cstdint>
#include <elfio/elfio.hpp>
#include <fstream>
#include <iostream>
#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

std::unordered_map<uint64_t, std::vector<std::string>> paramsMap;

std::unordered_map<std::string, uint64_t> funcMap;

Dwarf_Die getDieFromOffset(Dwarf_Debug dbg, Dwarf_Off offset,
                           Dwarf_Bool is_info) {
  Dwarf_Error error = nullptr;
  Dwarf_Die return_die = nullptr;

  int result = dwarf_offdie_b(dbg, offset, is_info, &return_die, &error);
  if (result == DW_DLV_OK) {
    // std::cout << "return_die: " << return_die << std::endl;
    return return_die;
  } else {
    // std::cout << "result: " << result << std::endl;
    return nullptr;
  }
}

std::string getNameFromAbstractOrigin(Dwarf_Debug dbg, Dwarf_Die die) {
  Dwarf_Error error;
  Dwarf_Attribute abstract_origin_attr;
  Dwarf_Off abstract_origin_offset;
  Dwarf_Die abstract_origin_die;
  char *name = nullptr;

  if (dwarf_attr(die, DW_AT_abstract_origin, &abstract_origin_attr, &error) !=
      DW_DLV_OK) {
    return "";
  }

  if (dwarf_global_formref(abstract_origin_attr, &abstract_origin_offset,
                           &error) != DW_DLV_OK) {
    dwarf_dealloc(dbg, abstract_origin_attr, DW_DLA_ATTR);
    return "";
  }

  if (dwarf_offdie_b(dbg, abstract_origin_offset, DW_DLV_OK,
                     &abstract_origin_die, &error) != DW_DLV_OK) {
    dwarf_dealloc(dbg, abstract_origin_attr, DW_DLA_ATTR);
    return "";
  }

  Dwarf_Attribute name_attr;
  if (dwarf_attr(abstract_origin_die, DW_AT_name, &name_attr, &error) ==
      DW_DLV_OK) {
    if (dwarf_formstring(name_attr, &name, &error) == DW_DLV_OK) {
      // std::cout << "name: " << name << std::endl;
      std::string result(name);
      dwarf_dealloc(dbg, name, DW_DLA_STRING);
      dwarf_dealloc(dbg, name_attr, DW_DLA_ATTR);
      dwarf_dealloc(dbg, abstract_origin_die, DW_DLA_DIE);
      dwarf_dealloc(dbg, abstract_origin_attr, DW_DLA_ATTR);
      return result;
    }
    dwarf_dealloc(dbg, name_attr, DW_DLA_ATTR);
  }

  dwarf_dealloc(dbg, abstract_origin_die, DW_DLA_DIE);
  dwarf_dealloc(dbg, abstract_origin_attr, DW_DLA_ATTR);
  return "";
}

void processSubprogram(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Bool is_info) {
  Dwarf_Error error;
  Dwarf_Attribute low_pc_attr;
  Dwarf_Addr low_pc = 0;

  if (dwarf_attr(die, DW_AT_low_pc, &low_pc_attr, &error) == DW_DLV_OK) {
    if (dwarf_formaddr(low_pc_attr, &low_pc, &error) != DW_DLV_OK) {
      return;
    }
    dwarf_dealloc(dbg, low_pc_attr, DW_DLA_ATTR);
  }

  std::vector<std::string> params;
  Dwarf_Die child_die;

  if (dwarf_child(die, &child_die, &error) == DW_DLV_OK) {
    do {
      Dwarf_Half tag;
      if (dwarf_tag(child_die, &tag, &error) != DW_DLV_OK) {
        std::cerr << "Error in dwarf_tag" << std::endl;
        continue;
      }

      if (tag == DW_TAG_formal_parameter) {
        Dwarf_Attribute name_attr;
        char *paramName = nullptr;
        bool nameFound = false;

        if (dwarf_attr(child_die, DW_AT_name, &name_attr, &error) ==
            DW_DLV_OK) {
          if (dwarf_formstring(name_attr, &paramName, &error) == DW_DLV_OK) {
            nameFound = true;
            params.push_back(paramName);
            dwarf_dealloc(dbg, paramName, DW_DLA_STRING);
          }
          dwarf_dealloc(dbg, name_attr, DW_DLA_ATTR);
        }

        // try to get name from abstract_origin
        if (!nameFound) {
          Dwarf_Attribute abstract_origin_attr;
          Dwarf_Off abstract_origin_offset;

          if (dwarf_attr(child_die, DW_AT_abstract_origin,
                         &abstract_origin_attr, &error) == DW_DLV_OK) {
            if (dwarf_global_formref(abstract_origin_attr,
                                     &abstract_origin_offset,
                                     &error) == DW_DLV_OK) {
              Dwarf_Die abstract_origin_die =
                  getDieFromOffset(dbg, abstract_origin_offset, is_info);
              if (abstract_origin_die) {
                Dwarf_Attribute name_attr;
                if (dwarf_attr(abstract_origin_die, DW_AT_name, &name_attr,
                               &error) == DW_DLV_OK) {
                  if (dwarf_formstring(name_attr, &paramName, &error) ==
                      DW_DLV_OK) {
                    // std::cout << "name: " << paramName << std::endl;
                    params.push_back(paramName);
                    dwarf_dealloc(dbg, paramName, DW_DLA_STRING);
                  }
                  dwarf_dealloc(dbg, name_attr, DW_DLA_ATTR);
                }
                dwarf_dealloc(dbg, abstract_origin_die, DW_DLA_DIE);
              }
            }
            dwarf_dealloc(dbg, abstract_origin_attr, DW_DLA_ATTR);
          }
        }
      }

      Dwarf_Die sibling_die;
      if (dwarf_siblingof_b(dbg, child_die, is_info, &sibling_die, &error) ==
          DW_DLV_OK) {
        dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
        child_die = sibling_die;
      } else {
        break;
      }
    } while (true);
  }

  if (low_pc) {
    paramsMap[low_pc] = params;
  }
}

void DIEDataExtractor(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Bool is_info) {
  Dwarf_Error error;
  Dwarf_Half tag;
  int result;

  std::stack<Dwarf_Die> dies;
  dies.push(die);

  while (!dies.empty()) {
    Dwarf_Die current_die = dies.top();
    dies.pop();

    result = dwarf_tag(current_die, &tag, &error);
    if (result != DW_DLV_OK) {
      continue;
    }

    if (tag == DW_TAG_subprogram) {
      processSubprogram(dbg, current_die, is_info);
    }

    Dwarf_Die child_die, sibling_die;
    if (dwarf_child(current_die, &child_die, &error) == DW_DLV_OK) {
      dies.push(child_die);
    }

    if (dwarf_siblingof_b(dbg, current_die, is_info, &sibling_die, &error) ==
        DW_DLV_OK) {
      dies.push(sibling_die);
    }

    if (current_die != die) {
      dwarf_dealloc(dbg, current_die, DW_DLA_DIE);
    }
  }
}

void processDwarf(const char *elf_file) {
  Dwarf_Debug dbg = 0;
  Dwarf_Error error;
  Dwarf_Handler errhand = 0;
  Dwarf_Ptr errarg = 0;

  char true_path_out_buffer[1024];
  unsigned int true_path_bufferlen = 1024;

  const char *reserved1 = NULL;
  Dwarf_Unsigned reserved2 = 0;
  Dwarf_Unsigned *reserved3 = NULL;

  if (dwarf_init_path(elf_file, true_path_out_buffer, true_path_bufferlen,
                      DW_GROUPNUMBER_ANY, errhand, errarg, &dbg,
                      &error) != DW_DLV_OK) {
    std::cerr << "Failed to initialize DWARF debug context." << std::endl;
    return;
  }

  Dwarf_Unsigned cu_header_length, abbrev_offset, next_cu_header;
  Dwarf_Half version_stamp, address_size, length_size, extension_size;
  Dwarf_Half header_cu_type;
  Dwarf_Sig8 type_signature;
  Dwarf_Unsigned typeoffset;

  Dwarf_Bool is_info = true;

  while (dwarf_next_cu_header_d(dbg, is_info, &cu_header_length, &version_stamp,
                                &abbrev_offset, &address_size, &length_size,
                                &extension_size, &type_signature, &typeoffset,
                                &next_cu_header, &header_cu_type,
                                &error) == DW_DLV_OK) {
    Dwarf_Die no_die = 0, cu_die;

    if (dwarf_siblingof_b(dbg, no_die, is_info, &cu_die, &error) == DW_DLV_OK) {
      DIEDataExtractor(dbg, cu_die, is_info);
      dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
    }
  }

  dwarf_finish(dbg);
}

void processFunc(const char *elf_file) {
  ELFIO::elfio reader;

  if (!reader.load(elf_file)) {
    std::cerr << "Can't find or process ELF file " << elf_file << std::endl;
    exit(1);
  }

  const ELFIO::symbol_section_accessor symbols(reader,
                                               reader.sections[".symtab"]);

  for (unsigned int i = 0; i < symbols.get_symbols_num(); ++i) {
    std::string name;
    ELFIO::Elf64_Addr value;
    ELFIO::Elf_Xword size;
    unsigned char bind;
    unsigned char type;
    ELFIO::Elf_Half section_index;
    unsigned char other;

    symbols.get_symbol(i, name, value, size, bind, type, section_index, other);

    if (type == ELFIO::STT_FUNC && value != 0) {
      funcMap[name] = value;
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cout << "Usage: " << argv[0] << " <ELF file>"
              << " <Output file>" << std::endl;
    return 1;
  }

  const char *elf_file = argv[1];
  const char *out_file = argv[2];

  processFunc(elf_file);
  processDwarf(elf_file);

  std::ofstream outfile(out_file);
  if (!outfile.is_open()) {
    std::cerr << "Failed to open output file " << out_file << std::endl;
    return 1;
  }

  for (auto &item : funcMap) {
    if (paramsMap.find(item.second) == paramsMap.end()) {
      continue;
    }
    outfile << item.first << ": 0x" << std::hex << item.second << " : [";
    auto &params = paramsMap[item.second];
    if (!params.empty()) {
      for (size_t i = 0; i < params.size(); ++i) {
        outfile << params[i];
        if (i < params.size() - 1) {
          outfile << ", ";
        }
      }
    }
    outfile << "]" << std::endl;
  }

  outfile.close();

  return 0;
}
