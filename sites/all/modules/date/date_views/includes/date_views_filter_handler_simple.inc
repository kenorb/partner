<?php
/**
 * @file
 * A standard Views filter for a single date field, using Date API form selectors and sql handling.
 */

class date_views_filter_handler_simple extends views_handler_filter_date {
  var $date_handler = NULL;
  var $offset = NULL;

  function init(&$view, &$options) {
    parent::init($view, $options);
    module_load_include('inc', 'date_api', 'date_api_sql');
    $this->date_handler = new date_sql_handler(DATE_UNIX);
    if (!empty($this->definition['field_name'])) {
      $field = field_info_field($this->definition['field_name']);
      if (!empty($field) && !empty($field['type'])) {
        $this->date_handler->date_type = $field['type'];
      }
      $this->date_handler->db_timezone = date_get_timezone_db($field['settings']['tz_handling']);
      $this->date_handler->local_timezone = date_get_timezone($field['settings']['tz_handling']);
    }
    $this->form_submitted = FALSE;
    $this->date_handler->granularity = isset($options['granularity']) ? $options['granularity'] : 'day';
    $this->format = $this->date_handler->views_formats($this->options['granularity'], 'sql');

    // Identify the base table for this field.
    // It will be used to call for the right query field options.
    $this->base_table = $this->table;

  }

  // Set default values for the date filter.
  function option_definition() {
    $options = parent::option_definition();
    $options['granularity'] = array('default' => 'day');
    $options['form_type'] = array('default' => 'date_select');
    $options['default_date'] = array('default' => '');
    $options['default_to_date'] = array('default' => '');
    $options['year_range'] = array('default' => '-3:+3');
    $options['add_delta'] = array('default' => '');
    return $options;
  }

  /**
   * Helper function to find a default value.
   */
  function date_default_value($prefix, $options = NULL) {
    $default_date = '';
    if (empty($options)) {
      $options = $this->options;
    }
    // If this is a remembered value, use the value from the SESSION.
    if (!empty($this->options['expose']['remember'])) {
      $display_id = ($this->view->display_handler->is_defaulted('filters')) ? 'default' : $this->view->current_display;
      if (!empty($_SESSION['views'][$this->view->name][$display_id]['date_filter'][$prefix])) {
        return $_SESSION['views'][$this->view->name][$display_id]['date_filter'][$prefix];
      }
    }

    // This is a date that needs to be constructed from options like 'now' .
    $default_option = $prefix == 'max' ? $options['default_to_date'] : $options['default_date'];
    if (!empty($default_option)) {
      str_replace('now', 'today', $default_option);
      $date = date_create($default_option, date_default_timezone_object());
      $default_date = !empty($date) ? $date->format($this->format) : '';

      // The format for our filter is in ISO format, but the widget will need it in datetime format.
      $default_date = str_replace('T', ' ', $default_date);
    }
    // This a fixed date.
    else {
      $default_date = $options['value'][$prefix];
    }
    return $default_date;
  }

  /**
   * Helper function to see if we need to swap in the default value.
   *
   * Views exposed filters treat everything as submitted, so if it's an empty value we have to
   * see if anything actually was submitted. If nothing has really been submitted, we need
   * to swap in our default value logic.
   */
  function get_filter_value($prefix, $input) {
    // All our date widgets provide datetime values but we use ISO in our SQL
    // for consistency between the way filters and arguments work (arguments
    // cannot contain spaces).
    if (empty($input)) {
      if (empty($this->options['exposed'])) {
        return str_replace(' ', 'T', $this->date_default_value($prefix));
      }
      elseif (isset($this->options['expose']['identifier']) && !isset($_GET[$this->options['expose']['identifier']])) {
        return str_replace(' ', 'T', $this->date_default_value($prefix));
      }
    }

    return str_replace(' ', 'T', $input);
  }

  function accept_exposed_input($input) {
    if (!empty($this->options['exposed'])) {
      $element_input = $input[$this->options['expose']['identifier']];
      $element_input['value'] = $this->get_filter_value('value', !empty($element_input['value']) ? $element_input['value'] : '');
      $element_input['min'] = $this->get_filter_value('min', !empty($element_input['min']) ? $element_input['min'] : '');
      $element_input['max'] = $this->get_filter_value('max', !empty($element_input['max']) ? $element_input['max'] : '');
      unset($element_input['default_date']);
      unset($element_input['default_to_date']);

      $input[$this->options['expose']['identifier']] = $element_input;
    }
    return parent::accept_exposed_input($input);

  }

  function op_between($field) {

    // Add the delta field to the view so we can later find the value that matched our query.
    list($table_name, $field_name) = explode('.', $field);
    if (!empty($this->options['add_delta']) && (substr($field_name, -6) == '_value' || substr($field_name, -7) == '_value2')) {
      $this->query->add_field($table_name, 'delta');
      $real_field_name = str_replace(array('_value', '_value2'), '', $this->real_field);
      $this->query->add_field($table_name, 'entity_id', 'date_id_' . $real_field_name);
      $this->query->add_field($table_name, 'delta', 'date_delta_' . $real_field_name);
    }

    $min_value = $this->get_filter_value('min', $this->value['min']);
    $min_comp_date = new DateObject($min_value, date_default_timezone(), $this->format);
    $max_value = $this->get_filter_value('max', $this->value['max']);
    $max_comp_date = new DateObject($max_value, date_default_timezone(), $this->format);
    $field_min = $this->date_handler->sql_field($field, NULL, $min_comp_date);
    $field_min = $this->date_handler->sql_format($this->format, $field_min);
    $field_max = $this->date_handler->sql_field($field, NULL, $max_comp_date);
    $field_max = $this->date_handler->sql_format($this->format, $field_max);
    $placeholder_min = $this->placeholder();
    $placeholder_max = $this->placeholder();
    $group = !empty($this->options['date_group']) ? $this->options['date_group'] : $this->options['group'];
    if ($this->operator == 'between') {
      $this->query->add_where_expression($group, "$field_min >= $placeholder_min AND $field_max <= $placeholder_max", array($placeholder_min => $min_value, $placeholder_max => $max_value));
    }
    else {
      $this->query->add_where_expression($group, "$field_min < $placeholder_min OR $field_max > $placeholder_max", array($placeholder_min => $min_value, $placeholder_max => $max_value));
    }
  }

  function op_simple($field) {

    // Add the delta field to the view so we can later find the value that matched our query.
    list($table_name, $field_name) = explode('.', $field);
    if (!empty($this->options['add_delta']) && (substr($field_name, -6) == '_value' || substr($field_name, -7) == '_value2')) {
      $this->query->add_field($table_name, 'delta');
      $real_field_name = str_replace(array('_value', '_value2'), '', $this->real_field);
      $this->query->add_field($table_name, 'entity_id', 'date_id_' . $real_field_name);
      $this->query->add_field($table_name, 'delta', 'date_delta_' . $real_field_name);
    }

    $value = $this->get_filter_value('value', $this->value['value']);
    $comp_date = new DateObject($value, date_default_timezone(), $this->format);
    $field = $this->date_handler->sql_field($field, NULL, $comp_date);
    $field = $this->date_handler->sql_format($this->format, $field);
    $placeholder = $this->placeholder();
    $group = !empty($this->options['date_group']) ? $this->options['date_group'] : $this->options['group'];
    $this->query->add_where_expression($group, "$field $this->operator $placeholder", array($placeholder => $value));
  }

  /**
   * Set the granularity of the date parts to use in the filter.
    */
  function has_extra_options() { return TRUE; }

  /**
   * Date selection options.
   */
  function widget_options() {
    $options = array(
      'date_select' => t('Select'),
      'date_text' => t('Text'),
      'date_popup' => t('Popup'),
      );
    if (!module_exists('date_popup')) {
      unset($options['date_popup']);
    }
    return $options;
  }

  function year_range() {
    $year_range = explode(':', $this->options['year_range']);
    if (substr($this->options['year_range'], 0, 1) == '-' || $year_range[0] < 0) {
      $this_year = date_format(date_now(), 'Y');
      $year_range[0] = $this_year + $year_range[0];
      $year_range[1] = $this_year + $year_range[1];
    }
    return $year_range;
  }

  function extra_options_form(&$form, &$form_state) {
    parent::extra_options_form($form, $form_state);
    $form['form_type'] = array(
      '#type' => 'radios',
      '#title' => t('Date selection form element'),
      '#default_value' => $this->options['form_type'],
      '#options' => $this->widget_options(),
      );

    $form['granularity'] = $this->date_handler->granularity_form($this->options['granularity']);
    $form['granularity']['#title'] = t('Filter granularity');

    $form['year_range'] = array(
      '#type' => 'date_year_range',
      '#default_value' => $this->options['year_range'],
    );

    if (!empty($this->definition['field_name'])) {
      $field = field_info_field($this->definition['field_name']);
    }
    $form['add_delta'] = array(
      '#type' => 'radios',
      '#title' => t('Add multiple value identifier'),
      '#default_value' => $this->options['add_delta'],
      '#options' => array('' => t('No'), 'yes' => t('Yes')),
      '#description' => t('Add an identifier to the view to show which multiple value date fields meet the filter criteria. Note: This option may introduce duplicate values into the view. Required when using multiple value fields in a Calendar or any time you want the node view of multiple value dates to display only the values that match the view filters.'),
      // Only let mere mortals tweak this setting for multi-value fields
      '#access' => !empty($field) ? $field['cardinality'] != 1 : 0,
    );
  }

  function extra_options_validate($form, &$form_state) {
    if (!preg_match('/^(?:\-[0-9]{1,4}|[0-9]{4}):(?:[\+|\-][0-9]{1,4}|[0-9]{4})$/', $form_state['values']['options']['year_range'])) {
      form_error($form['year_range'], t('Date year range must be in the format -9:+9, 2005:2010, -9:2010, or 2005:+9'));
    }
  }

  /**
   * Add the selectors to the value form using the date handler.
   */
  function value_form(&$form, &$form_state) {
    // We use different values than the parent form, so we must
    // construct our own form element.
    $form['value'] = array();
    $form['value']['#tree'] = TRUE;

    // Below section copied from views_handler_filter_numeric.inc.
    $which = 'all';
    $source = '';
    if (!empty($form['operator'])) {
      $source = ($form['operator']['#type'] == 'radios') ? 'radio:options[operator]' : 'edit-options-operator';
    }

    $identifier = $this->options['expose']['identifier'];
    if (!empty($form_state['exposed'])) {

      if (empty($this->options['expose']['use_operator']) || empty($this->options['expose']['operator_id'])) {
        // exposed and locked.
        $which = in_array($this->operator, $this->operator_values(2)) ? 'minmax' : 'value';
      }
      else {
        $source = 'edit-' . drupal_html_id($this->options['expose']['operator_id']);
      }
    }

    if ($which == 'all' || $which == 'value') {
      $form['value'] += $this->date_parts_form($form_state, 'value', $source, $which, $this->operator_values(1), $identifier, 'default_date');
    }

    if ($which == 'all' || $which == 'minmax') {
      $form['value'] += $this->date_parts_form($form_state, 'min', $source, $which, $this->operator_values(2), $identifier, 'default_date');
      $form['value'] += $this->date_parts_form($form_state, 'max', $source, $which, $this->operator_values(2), $identifier, 'default_to_date');
    }

    // Add some extra validation for the select widget to be sure that
    // the user inputs all parts of the date.
    if ($this->options['form_type'] == 'date_select') {
      $form['value']['#element_validate'] = array('date_views_select_validate');
    }

  }

  /**
   * A form element to select date part values.
   *
   * @param string $prefix
   *   A prefix for the date values, 'value', 'min', or 'max' .
   * @param string $source
   *   The operator for this element.
   * @param string $which
   *   Which element to provide, 'all', 'value', or 'minmax' .
   * @param array $operator_values
   *   An array of the allowed operators for this element.
   * @param array $identifier
   *   Identifier of the exposed element.
   * @param array $relative_id
   *   Form element id to use for the relative date field.
   *
   * @return
   *   The form date part element for this instance.
   */
  function date_parts_form($form_state, $prefix, $source, $which, $operator_values, $identifier, $relative_id) {
    module_load_include('inc', 'date_api', 'date_api_elements');
    switch ($prefix) {
      case 'min':
        $label = t('Start date');
        $relative_label = t('Relative start date');
        break;
      case 'max':
        $label = t('End date');
        $relative_label = t('Relative end date');
        break;
      default:
        $label = '';
        $relative_label = t('Relative date');
        break;
    }

    $type = $this->options['form_type'];
    if ($type == 'date_popup' && !module_exists('date_popup')) {
      $type = 'date_text';
    }

    $format = $this->date_handler->views_formats($this->options['granularity'], 'display');
    $granularity = array_keys($this->date_handler->date_parts($this->options['granularity']));
    $relative_value = ($prefix == 'max' ? $this->options['default_to_date'] : $this->options['default_date']);

    if (!empty($form_state['exposed'])) {
      // UI when the date selector is exposed.
      $default_date = $this->date_default_value($prefix);
      $id = 'edit-' . str_replace('_', '-', $this->field) . '-' . $prefix;
      $form[$prefix] = array(
        '#title' => check_plain($label),
        '#type' => $type,
        '#size' => 20,
        '#default_value' => !empty($this->value[$prefix]) ? $this->value[$prefix] : $default_date,
        '#date_format' => date_limit_format($format, $granularity),
        '#date_label_position' => 'within',
        '#date_year_range' => $this->options['year_range'],
        '#process' => array($type . '_element_process'),
        '#prefix' => '<div id="' . $id . '-wrapper"><div id="' . $id . '">',
        '#suffix' => '</div></div>',
      );
      if ($which == 'all') {
        $form[$prefix]['#pre_render'][] = 'ctools_dependent_pre_render';
        $form[$prefix]['#dependency'] = array($source => $operator_values);
      }
      if (!isset($form_state['input'][$identifier][$prefix])) {
        $form_state['input'][$identifier][$prefix] = $this->value[$prefix];
      }
    }
    else {
      // UI when the date selector is on the views configuration screen.
      $default_date = '';
      $id = 'edit-options-value-' . $prefix;
      $form[$prefix . '_group'] = array(
        '#type' => 'fieldset',
        '#attributes' => array('class' => array('date-views-filter-fieldset')),
      );
      $form[$prefix . '_group'][$prefix . '_choose_input_type'] = array(
        '#title' => check_plain($label),
        '#type' => 'select',
        '#options' => array('date' => t('Select a date'), 'relative' => ('Enter a relative date')),
        '#attributes' => array('class' => array($prefix . '-choose-input-type')),
        '#default_value' => !empty($relative_value) ? 'relative' : 'date',
      );
      $form[$prefix . '_group'][$prefix] = array(
        '#title' => t('Select a date'),
        '#type' => $type,
        '#size' => 20,
        '#default_value' => !empty($this->value[$prefix]) ? $this->value[$prefix] : $default_date,
        '#date_format' => date_limit_format($format, $granularity),
        '#date_label_position' => 'within',
        '#date_year_range' => $this->options['year_range'],
        '#process' => array($type . '_element_process'),
        '#prefix' => '<div id="' . $id . '-wrapper"><div id="' . $id . '">',
        '#suffix' => '</div></div>',
        '#states' => array(
          'visible' => array(
            ":input.{$prefix}-choose-input-type" => array('value' => 'date'),
          ),
        ),
      );
      $form[$prefix . '_group'][$relative_id] = array(
        '#type' => 'textfield',
        '#title' => check_plain($relative_label),
        '#default_value' => $relative_value,
        '#description' => t("Relative dates are computed when the view is displayed. Examples: now, now +1 day, 12AM today, Monday next week. <a href=\"@relative_format\">More examples of relative date formats in the PHP documentation</a>.", array('@relative_format' => 'http://www.php.net/manual/en/datetime.formats.relative.php')),
        '#states' => array(
          'visible' => array(
            ":input.{$prefix}-choose-input-type" => array('value' => 'relative'),
          ),
        ),
      );
      if ($which == 'all') {
        $form[$prefix . '_group']['#pre_render'][] = 'ctools_dependent_pre_render';
        $form[$prefix . '_group']['#dependency'] = array($source => $operator_values);
      }
    }
    return $form;
  }

  /**
   * Value validation.
   *
   * TODO add in more validation.
   *
   * We are setting an extra option using a value form
   * because it makes more sense to set it there.
   * That's not the normal method, so we have to manually
   * transfer the selected value back to the option.
   */
  function value_validate($form, &$form_state) {

    $options = &$form_state['values']['options'];

    if ($options['operator'] == 'between' || $options['operator'] == 'not between') {
      if ($options['value']['min_group']['min_choose_input_type'] == 'relative') {
        if (empty($options['value']['min_group']['default_date'])) {
          form_set_error('options][value][min_group][default_date', t('Relative start date not specified.'));
        }
        else {
          $this->options['default_date'] = $options['value']['min_group']['default_date'];
          // NULL out the value field, user wanted the relative value to take hold.
          $options['value']['min_group']['min'] = NULL;
        }
      }
      // If an absolute date was used, be sure to wipe the relative date.
      else {
        $this->options['default_date'] = '';
      }
      if ($options['value']['max_group']['max_choose_input_type'] == 'relative') {
        if (empty($options['value']['max_group']['default_to_date'])) {
          form_set_error('options][value][max_group][default_to_date', t('Relative end date not specified.'));
        }
        else {
          $this->options['default_to_date'] = $options['value']['max_group']['default_to_date'];
          // NULL out the value field, user wanted the relative value to take hold.
          $options['value']['max_group']['max'] = NULL;
        }
      }
      // If an absolute date was used, be sure to wipe the relative date.
      else {
        $this->options['default_to_date'] = '';
      }
    }
    elseif (in_array($options['operator'], array('<', '<=', '=', '!=', '>=', '>'))) {
      if ($options['value']['value_group']['value_choose_input_type'] == 'relative') {
        if (empty($options['value']['value_group']['default_date'])) {
          form_set_error('options][value][value_group][default_date', t('Relative date not specified.'));
        }
        else {
          $this->options['default_date'] = $options['value']['value_group']['default_date'];
          // NULL out the value field, user wanted the relative value to take hold.
          $options['value']['value_group']['value'] = NULL;
        }
      }
      // If an absolute date was used, be sure to wipe the relative date.
      else {
        $this->options['default_date'] = '';
      }
    }
    // Flatten the form structure for views, so the values can be saved.
    foreach (array('value', 'min', 'max') as $key) {
      $options['value'][$key] = $options['value'][$key . '_group'][$key];
    }
  }

  /**
   * Validate that the time values convert to something usable.
   */
  function validate_valid_time(&$form, $operator, $value) {
    // Override the core date filter validation.
    // Our date widgets do their own validation.
  }

  // Update the summary values to provide
  // meaningful information for each option.
  function admin_summary() {
    $parts = $this->date_handler->date_parts();
    $widget_options = $this->widget_options();
    // If the filter is exposed, display the granularity.
    if ($this->options['exposed']) {
      return t('<strong>Exposed</strong> @widget @format', array('@format' => $parts[$this->date_handler->granularity], '@widget' => $widget_options[$this->options['form_type']]));
    }
    // If not exposed, display the value.
    $output = '';
    if (in_array($this->operator, $this->operator_values(2))) {
      $min = check_plain(!empty($this->options['default_date']) ? $this->options['default_date'] : $this->options['value']['min']);
      $max = check_plain(!empty($this->options['default_to_date']) ? $this->options['default_to_date'] : $this->options['value']['max']);
      $output .= t('@min and @max', array('@min' => $min, '@max' => $max));
    }
    else {
      $output .= check_plain(!empty($this->options['default_date']) ? $this->options['default_date'] : $this->options['value']['value']);
    }
    return $output;
  }

}
