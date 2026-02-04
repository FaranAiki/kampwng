return {
  {
    "nvim-treesitter/nvim-treesitter",
    opts = function(_, opts)
      -- 1. Register the filetype
      vim.filetype.add({
        extension = {
          aky = "alkyl",
        },
      })

      -- 2. Register the Tree-sitter parser
      local parser_config = require("nvim-treesitter.parsers").get_parser_configs()
      
      parser_config.alkyl = {
        install_info = {
          -- IMPORTANT: Adjust this path if you cloned tree-sitter-alkyl elsewhere
          -- vim.fn.expand("~") ensures it works with the home directory symbol
          url = vim.fn.expand("~") .. "/tree-sitter-alkyl", 
          files = { "src/parser.c" }, 
          branch = "main",
          generate_requires_npm = false,
          requires_generate_from_grammar = false,
        },
        filetype = "alkyl", 
      }

      -- 3. (Optional) Ensure it doesn't try to download it from the internet
      -- NvChad configures ensure_installed in its default options. 
      -- Since this is local, we don't strictly need to add it there, 
      -- but we must manually run :TSInstall alkyl once.
    end,
  }
}
