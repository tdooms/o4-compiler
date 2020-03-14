//============================================================================
// @author      : Thomas Dooms & Ward Gauderis
// @date        : 3/10/20
// @copyright   : BA2 Informatica - Thomas Dooms & Ward Gauderis - University of Antwerp
//============================================================================

#include "dotVisitor.h"
#include "errors.h"

DotVisitor::DotVisitor(std::filesystem::path path, const std::vector<std::string>* names) : path(std::move(path)),
                                                                                            names(names) {
	std::filesystem::create_directory(this->path.parent_path());
	stream = std::ofstream(this->path.string() + ".dot");
	if (not stream.is_open()) throw CompilationError("could not open file: " + this->path.string() + ".dot");

	stream << "digraph G\n";
	stream << "{\n";
}

DotVisitor::~DotVisitor() {
	stream << "\"0\"[style = invis];\n";
	stream << "}\n" << std::flush;
	stream.close();

	const auto dot = path.string() + ".dot";
	const auto png = path.string() + ".png";

	system(("dot -Tpng " + dot + " -o " + png).c_str());
//	std::filesystem::remove(dot);
}

void DotVisitor::linkWithParent(antlr4::tree::ParseTree* context, const std::string& name) {
	stream << '"' << context->parent << "\" -> \"" << context << "\";\n";
	stream << '"' << context << "\"[label=\"" + name + "\"];\n";
}

antlrcpp::Any DotVisitor::visitChildren(antlr4::tree::ParseTree* node) {
	if (auto n = dynamic_cast<antlr4::RuleContext*>(node)) {
		linkWithParent(node, (*names)[n->getRuleIndex()]);
	} else {
		throw WhoopsiePoopsieError("Geen RuleContext");
	}
	return antlr4::tree::AbstractParseTreeVisitor::visitChildren(node);
}

antlrcpp::Any DotVisitor::visitTerminal(antlr4::tree::TerminalNode* node) {
	linkWithParent(node, node->getText());
	return defaultResult();
}