#ifndef BUILDER_POLICY_STAGEVALIDATOR_HPP
#define BUILDER_POLICY_STAGEVALIDATOR_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <base/dotPath.hpp>
#include <base/json.hpp>
#include <base/name.hpp>
#include <fmt/format.h>

#include "syntax.hpp"

namespace builder::policy
{
namespace detail
{

enum class AssetType
{
    Decoder,
    Filter,
    Output
};

struct StageRule
{
    std::string_view name;
    bool isPrefix;
};

static constexpr std::string_view PARSE_STAGE_PREFIX {"parse|"};

static inline std::string_view assetTypeName(const AssetType assetType)
{
    switch (assetType)
    {
        case AssetType::Decoder:
            return "decoder";
        case AssetType::Filter:
            return "filter";
        case AssetType::Output:
            return "output";
    }

    return "unknown";
}

static inline AssetType assetTypeFromName(const base::Name& name)
{
    if (syntax::name::isDecoder(name))
    {
        return AssetType::Decoder;
    }

    if (syntax::name::isFilter(name))
    {
        return AssetType::Filter;
    }

    if (syntax::name::isOutput(name))
    {
        return AssetType::Output;
    }

    throw std::runtime_error(fmt::format("Unknown asset type for asset '{}'", name.toStr()));
}

static inline std::string_view allowedStages(const AssetType assetType)
{
    switch (assetType)
    {
        case AssetType::Decoder:
            return "check, parse|<field>, normalize";
        case AssetType::Filter:
            return "check";
        case AssetType::Output:
            return "check, outputs";
    }

    return "";
}

static inline const std::array<StageRule, 3>& decoderStageRules()
{
    static const std::array<StageRule, 3> rules {{
        {std::string_view {syntax::asset::CHECK_KEY}, false},
        {PARSE_STAGE_PREFIX, true},
        {std::string_view {syntax::asset::NORMALIZE_KEY}, false},
    }};

    return rules;
}

static inline const std::array<StageRule, 1>& filterStageRules()
{
    static const std::array<StageRule, 1> rules {{
        {std::string_view {syntax::asset::CHECK_KEY}, false},
    }};

    return rules;
}

static inline const std::array<StageRule, 2>& outputStageRules()
{
    static const std::array<StageRule, 2> rules {{
        {std::string_view {syntax::asset::CHECK_KEY}, false},
        {std::string_view {syntax::asset::OUTPUTS_KEY}, false},
    }};

    return rules;
}

template<std::size_t N>
static inline bool isAllowedStage(const std::string_view stage, const std::array<StageRule, N>& rules)
{
    return std::any_of(rules.begin(),
                       rules.end(),
                       [stage](const auto& rule)
                       {
                           if (rule.isPrefix)
                           {
                               return stage.rfind(rule.name, 0) == 0;
                           }

                           return stage == rule.name;
                       });
}

static inline void throwInvalidStage(const std::string_view stage,
                                     const base::Name& assetName,
                                     const AssetType assetType)
{
    throw std::runtime_error(fmt::format("Invalid stage '{}' for {} asset '{}'. Allowed stages: {}",
                                         stage,
                                         assetTypeName(assetType),
                                         assetName.toStr(),
                                         allowedStages(assetType)));
}

static inline bool isSupportedOutputOperation(const std::string_view operation)
{
    return operation == syntax::asset::FIRST_OF_KEY || operation == syntax::asset::FILE_OUTPUT_KEY
           || operation == syntax::asset::INDEXER_OUTPUT_KEY;
}

static inline void validateOutputsStage(const json::Json& outputs, const base::Name& assetName)
{
    const auto arr = outputs.getArray();

    if (!arr || arr.value().empty())
    {
        throw std::runtime_error(
            fmt::format("Invalid outputs stage for output asset '{}'. Expected a non-empty array of objects",
                        assetName.toStr()));
    }

    for (const auto& item : arr.value())
    {
        const auto obj = item.getObject();

        if (!obj)
        {
            throw std::runtime_error(
                fmt::format("Invalid outputs stage for output asset '{}'. Expected every item to be an object",
                            assetName.toStr()));
        }

        if (obj.value().size() != 1)
        {
            throw std::runtime_error(
                fmt::format("Invalid outputs stage for output asset '{}'. Each item must contain exactly one operation",
                            assetName.toStr()));
        }

        for (const auto& operationEntry : obj.value())
        {
            const auto& operation = std::get<0>(operationEntry);

            if (!isSupportedOutputOperation(operation))
            {
                throw std::runtime_error(
                    fmt::format("Invalid output operation '{}' for output asset '{}'.\n Allowed operations: {}, {}, {}",
                                operation,
                                assetName.toStr(),
                                syntax::asset::FIRST_OF_KEY,
                                syntax::asset::FILE_OUTPUT_KEY,
                                syntax::asset::INDEXER_OUTPUT_KEY));
            }
        }
    }
}

static inline void validateDecoderStage(const std::string& stage, const base::Name& assetName)
{
    const std::string_view stageView {stage};

    if (!isAllowedStage(stageView, decoderStageRules()))
    {
        throwInvalidStage(stageView, assetName, AssetType::Decoder);
    }

    if (stageView.rfind(PARSE_STAGE_PREFIX, 0) != 0)
    {
        return;
    }

    const auto field = stage.substr(PARSE_STAGE_PREFIX.size());

    if (field.empty())
    {
        throw std::runtime_error(fmt::format(
            "Invalid parse stage '{}' for decoder asset '{}': missing field. Expected format: parse|<field>",
            stage,
            assetName.toStr()));
    }

    try
    {
        DotPath {field};
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(
            fmt::format("Invalid parse stage '{}' for decoder asset '{}': {}", stage, assetName.toStr(), e.what()));
    }
}

static inline void validateFilterStage(const std::string& stage, const base::Name& assetName)
{
    const std::string_view stageView {stage};

    if (!isAllowedStage(stageView, filterStageRules()))
    {
        throwInvalidStage(stageView, assetName, AssetType::Filter);
    }
}

static inline void validateOutputStage(const std::string& stage, const json::Json& value, const base::Name& assetName)
{
    const std::string_view stageView {stage};

    if (!isAllowedStage(stageView, outputStageRules()))
    {
        throwInvalidStage(stageView, assetName, AssetType::Output);
    }

    if (stageView == syntax::asset::OUTPUTS_KEY)
    {
        validateOutputsStage(value, assetName);
    }
}

} // namespace detail

static inline void validateStages(const base::Name& assetName,
                                  const std::vector<std::tuple<std::string, json::Json>>& stages)
{
    const auto assetType = detail::assetTypeFromName(assetName);

    for (const auto& [stage, value] : stages)
    {
        if (stage == syntax::asset::DEFINITIONS_KEY)
        {
            continue;
        }

        switch (assetType)
        {
            case detail::AssetType::Decoder:
                detail::validateDecoderStage(stage, assetName);
                break;
            case detail::AssetType::Filter:
                detail::validateFilterStage(stage, assetName);
                break;
            case detail::AssetType::Output:
                detail::validateOutputStage(stage, value, assetName);
                break;
        }
    }
}

} // namespace builder::policy

#endif // BUILDER_POLICY_STAGEVALIDATOR_HPP

