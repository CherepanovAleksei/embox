# Generated by GOLD Parser Builder using Mybuild program template.

# Rule productions for 'ConfigFile' grammar.

include mk/mybuild/common-rules.mk

# Rule: <ConfigFile> ::= <Package> <Imports> <Type>
# Args: 1..3 - Symbols in the RHS.
define $(gold_grammar)_produce-ConfigFile
	$(for fileContent <- $(new CfgFileContentRoot),
		$(set fileContent->name,$1)
		$(set fileContent->imports,$2)
		$(set fileContent->configuration,$3)
		$(fileContent)
	)
endef

# Rule: <Package> ::= package <QualifiedName>
# Args: 1..2 - Symbols in the RHS.
$(gold_grammar)_produce-Package_package  = $2

# Rule: <Package> ::=
define $(gold_grammar)_produce-Package
	$(call gold_report_warning,
			Using default package)
endef

# Rule: <Import> ::= import <QualifiedNameWithWildcard>
$(gold_grammar)_produce-Import_import = $2

# Rule: <AnnotatedConfiguration> ::= <Annotations> <Configuration>
define $(gold_grammar)_produce-AnnotatedConfiguration
	$2
endef

# Rule: <Configuration> ::= configuration Identifier '{' <ConfigurationMembers> '}'
# Args: 1..5 - Symbols in the RHS.
define $(gold_grammar)_produce-Configuration_configuration_Identifier_LBrace_RBrace
	$(for cfg <- $(new CfgConfiguration),
		$(set cfg->name,$2)
		$(set cfg->origin,$(call gold_location_of,2))

		$(silent-foreach member,
				includes,
				$(set cfg->$(member),\
					$(filter-patsubst $(member)/%,%,$4)))

		$(cfg)
	)
endef

# Rule: <AnnotatedConfigurationMember> ::= <Annotations> <IncludeMember>
define $(gold_grammar)_produce-AnnotatedConfigurationMember
	$(for include <- $2,
		$(set include->annotations,$1)
		$(include))
endef

# Rule: <IncludeMember> ::= include <ReferenceWithInitializerList>
# Args: 1..2 - Symbols in the RHS.
define $(gold_grammar)_produce-IncludeMember_include
	$(for include <-$2,
		$(set include->origin,$(call gold_location_of,2)))
	$(addprefix $1s/,$2)
endef

# Rule: <ReferenceWithInitializerList> ::= <ReferenceWithInitializer> ',' <ReferenceWithInitializerList>
# Args: 1..3 - Symbols in the RHS.
$(gold_grammar)_produce-ReferenceWithInitializerList_Comma = $1 $3

# Rule: <ReferenceWithInitializer> ::= <Reference> <Initializer>
# Args: 1..2 - Symbols in the RHS.
define $(gold_grammar)_produce-ReferenceWithInitializer
	$(for link <- $1,
		include <- $(new CfgInclude),

		$(set include->module_links,$(link))
		$(set include->optionBindings,$2)

		$(include)
	)
endef

# Rule: <Initializer> ::= '(' <ParametersList> ')'
# Args: 1..3 - Symbols in the RHS.
$(gold_grammar)_produce-Initializer_LParan_RParan = $2


