#include <AggregateFunctions/AggregateFunctionFactory.h>
#include <AggregateFunctions/AggregateFunctionGroupArrayMoving.h>
#include <AggregateFunctions/Helpers.h>
#include <AggregateFunctions/FactoryHelpers.h>
#include <DataTypes/DataTypeDate.h>
#include <DataTypes/DataTypeDateTime.h>
#include <DataTypes/DataTypeDateTime64.h>


namespace DB
{
struct Settings;

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int BAD_ARGUMENTS;
}

namespace
{

/// TODO Proper support for Decimal256.
template <typename T, typename LimitNumberOfElements>
struct MovingSum
{
    using Data = MovingSumData<std::conditional_t<is_decimal<T>, Decimal128, NearestFieldType<T>>>;
    using Function = MovingImpl<T, LimitNumberOfElements, Data>;
};

template <typename T, typename LimitNumberOfElements>
struct MovingAvg
{
    using Data = MovingAvgData<std::conditional_t<is_decimal<T>, Decimal128, Float64>>;
    using Function = MovingImpl<T, LimitNumberOfElements, Data>;
};

template <typename T, typename LimitNumberOfElements> using MovingSumTemplate = typename MovingSum<T, LimitNumberOfElements>::Function;
template <typename T, typename LimitNumberOfElements> using MovingAvgTemplate = typename MovingAvg<T, LimitNumberOfElements>::Function;

template <template <typename, typename> class Function, typename HasLimit, typename DecimalArg, typename ... TArgs>
inline AggregateFunctionPtr createAggregateFunctionMovingImpl(const std::string & name, const DataTypePtr & argument_type, TArgs ... args)
{
    AggregateFunctionPtr res;

    if constexpr (DecimalArg::value)
        res.reset(createWithDecimalType<Function, HasLimit>(*argument_type, argument_type, std::forward<TArgs>(args)...));
    else
        res.reset(createWithNumericType<Function, HasLimit>(*argument_type, argument_type, std::forward<TArgs>(args)...));

    if (!res)
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Illegal type {} of argument for aggregate function {}",
                        argument_type->getName(), name);

    return res;
}

template <template <typename, typename> class Function>
AggregateFunctionPtr createAggregateFunctionMoving(
    const std::string & name, const DataTypes & argument_types, const Array & parameters, const Settings *)
{
    assertUnary(name, argument_types);

    bool limit_size = false;

    UInt64 max_elems = std::numeric_limits<UInt64>::max();

    if (parameters.empty())
    {
        // cumulative sum without parameter
    }
    else if (parameters.size() == 1)
    {
        auto type = parameters[0].getType();
        if (type != Field::Types::Int64 && type != Field::Types::UInt64)
               throw Exception(ErrorCodes::BAD_ARGUMENTS, "Parameter for aggregate function {} should be positive integer", name);

        if ((type == Field::Types::Int64 && parameters[0].get<Int64>() < 0) ||
            (type == Field::Types::UInt64 && parameters[0].get<UInt64>() == 0))
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Parameter for aggregate function {} should be positive integer", name);

        limit_size = true;
        max_elems = parameters[0].get<UInt64>();
    }
    else
        throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
            "Incorrect number of parameters for aggregate function {}, should be 0 or 1", name);

    const DataTypePtr & argument_type = argument_types[0];
    if (!limit_size)
    {
        if (isDecimal(argument_type))
            return createAggregateFunctionMovingImpl<Function, std::false_type, std::true_type>(name, argument_type);
        else
            return createAggregateFunctionMovingImpl<Function, std::false_type, std::false_type>(name, argument_type);
    }
    else
    {
        if (isDecimal(argument_type))
            return createAggregateFunctionMovingImpl<Function, std::true_type, std::true_type>(name, argument_type, max_elems);
        else
            return createAggregateFunctionMovingImpl<Function, std::true_type, std::false_type>(name, argument_type, max_elems);
    }
}

}


void registerAggregateFunctionMoving(AggregateFunctionFactory & factory)
{
    AggregateFunctionProperties properties = { .returns_default_when_only_null = false, .is_order_dependent = true };

    factory.registerFunction("groupArrayMovingSum", { createAggregateFunctionMoving<MovingSumTemplate>, properties });
    factory.registerFunction("groupArrayMovingAvg", { createAggregateFunctionMoving<MovingAvgTemplate>, properties });
}

}
