vim.api.nvim_create_autocmd("FileType", {
	pattern = { "c", "cpp" },
	callback = function()
		vim.opt_local.makeprg = "./nob && ./build/cowpaint"
		-- optional: a shortcut to run :make
		vim.keymap.set("n", "<leader>m", ":make<CR>", { buffer = true, desc = "Project Make" })
	end,
})
