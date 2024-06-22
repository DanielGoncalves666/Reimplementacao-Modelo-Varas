/* 
   File: main.c
   Author: Daniel Gonçalves
   Creation date: 2023-10-15
   Description: Contains the project's main function, which is responsible for calling the necessary functions to extract data from the input files, generate the structures, run the simulations and print some of the data.
*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<argp.h>
#include<unistd.h>

#include"../headers/exit.h"
#include"../headers/pedestrian.h"
#include"../headers/initialization.h"
#include"../headers/cli_processing.h"
#include"../headers/printing_utilities.h"
#include"../headers/shared_resources.h"

static Function_Status run_simulations(FILE *output_file);
static Function_Status conflict_solving();
static void deallocate_program_structures(FILE *output_file, FILE *auxiliary_file);

int main(int argc, char **argv){

    FILE *auxiliary_file = NULL;
    FILE *output_file = NULL;
    int simulation_set_quantity = 1; // Origins that use static exits have a single simulation set.
    int simulation_set_index = 0;
    int current_exit_number = 0;

    if(argp_parse(&argp, argc, argv,0,0,&cli_args) != 0)
        return END_PROGRAM;

    if(open_auxiliary_file(&auxiliary_file) == FAILURE)
        return END_PROGRAM;
    
    if(open_output_file( &output_file) == FAILURE)
    {
        if(auxiliary_file != NULL)
            fclose(auxiliary_file);
        return END_PROGRAM;
    }

    if(cli_args.environment_origin != AUTOMATIC_CREATED)
    {
        if(load_environment() == FAILURE)
            return END_PROGRAM;
    }
    else
    {
        if(generate_environment() == FAILURE)
            return END_PROGRAM;
    }

    print_full_command(output_file);

    if(auxiliary_file != NULL)
    {
        simulation_set_quantity = extract_simulation_set_quantity(auxiliary_file);
        if(simulation_set_quantity == -1)
            return END_PROGRAM;
    }

    do
    {
        if(origin_uses_auxiliary_data() == true)
        {
            if( get_next_simulation_set(auxiliary_file, &current_exit_number) == FAILURE)
                return END_PROGRAM;

            if(current_exit_number == 0)
                break; // All simulation sets were processed.
        }

        if(cli_args.show_simulation_set_info)
            print_simulation_set_information(output_file);

        int returned_value = calculate_final_floor_field();
        if( returned_value == FAILURE) 
            return END_PROGRAM;
        else if(returned_value == INACCESSIBLE_EXIT)
        {
            if(cli_args.output_format != OUTPUT_TIMESTEPS_COUNT)
                fprintf(output_file, "At least one exit from the simulation set is inaccessible.\n");
            else
                print_placeholder(output_file, -1);

            if(origin_uses_auxiliary_data() == true)
                deallocate_exits();

            print_execution_status(simulation_set_index, simulation_set_quantity);
            simulation_set_index++;

            continue;
        }

        // The actual simulation happens here.
        if(run_simulations(output_file) == FAILURE)
            return END_PROGRAM;

        if(origin_uses_auxiliary_data() == true)
            deallocate_exits();

        if(cli_args.output_format == OUTPUT_TIMESTEPS_COUNT)
            fprintf(output_file, "\n");

        if(cli_args.output_format == OUTPUT_HEATMAP)
        {
            print_heatmap(output_file);        
            reset_integer_grid(heatmap_grid, cli_args.global_line_number, cli_args.global_column_number);
        }


        print_execution_status(simulation_set_index, simulation_set_quantity);
        simulation_set_index++;

        if(origin_uses_static_exits() == true) // Only a single simulation set.
            break;

    }while(true);

    deallocate_program_structures(output_file, auxiliary_file);

    return END_PROGRAM;
}

/**
 * Runs all the simulations for a specific simulation set, printing generated data if appropriate.
 * 
 * @param output_file Stream where the output data will be written.
 * @return Function_Status: FAILURE (0) or SUCCESS (1).
*/
static Function_Status run_simulations(FILE *output_file)
{
    if(cli_args.single_exit_flag == true && cli_args.output_format == OUTPUT_TIMESTEPS_COUNT && exits_set.num_exits == 1)
    {
        fprintf(output_file, "#1 "); // simulation set where the exit was combined with itself. Used to correct errors in the plotting program.
    }

    for(int simu_index = 0; simu_index < cli_args.num_simulations; simu_index++, cli_args.seed++)
    {
        srand(cli_args.seed);

        if(cli_args.show_debug_information)
            print_double_grid(exits_set.final_floor_field);

        if(origin_uses_static_pedestrians() == false)
        {
            if( insert_pedestrians_at_random(cli_args.total_num_pedestrians) == FAILURE)
                return FAILURE;
        }
        
        if(cli_args.output_format == OUTPUT_VISUALIZATION)
            print_pedestrian_position_grid(output_file, simu_index, 0);

        int number_timesteps = 0;
        while(is_environment_empty() == false)
        {
            if(cli_args.show_debug_information)
                printf("\nTimestep %d.\n", number_timesteps + 1);
            
            evaluate_pedestrians_movements();
            determine_pedestrians_in_panic();
            
            if(!cli_args.allow_X_movement)
                block_X_movement(); // Runs when allow_X_movement is false.
            
            if(conflict_solving() == FAILURE)
                return FAILURE;
            
            apply_pedestrian_movement();

            update_pedestrian_position_grid();
            reset_pedestrian_state();
            reset_pedestrian_panic();
            
            number_timesteps++;

            if(cli_args.output_format == OUTPUT_VISUALIZATION)
            {
                if(!cli_args.write_to_file)
                    sleep(1);
                    
                print_pedestrian_position_grid(output_file, simu_index,number_timesteps);
            }

        }

        if(origin_uses_static_pedestrians() == true)
            reset_pedestrians_structures();
        else
            deallocate_pedestrians();

        if(cli_args.output_format == OUTPUT_TIMESTEPS_COUNT)
            fprintf(output_file,"%d ", number_timesteps);
    }

    return SUCCESS;
}

/**
 * Calls the necessary functions to identify and solve conflicts between pedestrians.
 * @return Function_Status: FAILURE (0) or SUCCESS (1).
*/
static Function_Status conflict_solving()
{
    Cell_Conflict pedestrian_conflicts = NULL;
    int num_conflicts = 0;

    if(identify_pedestrian_conflicts(&pedestrian_conflicts, &num_conflicts) == FAILURE)
        return FAILURE;                

    if(solve_pedestrian_conflicts(pedestrian_conflicts, num_conflicts) == FAILURE)
        return FAILURE;

    if(cli_args.show_debug_information)
        print_pedestrian_conflict_information(pedestrian_conflicts, num_conflicts);

    free(pedestrian_conflicts);

    return SUCCESS;
}

 /**
  * Close opened files and deallocate structures used throughout the program.
  * 
  * @param output_file
  * @param auxiliary_file
 */
static void deallocate_program_structures(FILE *output_file, FILE *auxiliary_file)
{
    if(auxiliary_file != NULL)
        fclose(auxiliary_file);

    if(output_file != NULL && output_file != stdout)
        fclose(output_file);

    deallocate_pedestrians();
    deallocate_exits();
    
    deallocate_grid((void **) environment_only_grid,cli_args.global_line_number);
    deallocate_grid((void **) pedestrian_position_grid,cli_args.global_line_number);
    deallocate_grid((void **) heatmap_grid,cli_args.global_line_number);
}